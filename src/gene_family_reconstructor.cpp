#include <cmath>
#include <memory>

#include "gene_family_reconstructor.h"
#include "lambda.h"
#include "matrix_cache.h"
#include "root_equilibrium_distribution.h"
#include "gene_family.h"

std::ostream& operator<<(std::ostream& ost, family_size_change fsc)
{
    switch (fsc)
    {
    case Increase:
        ost << "i";
        break;
    case Decrease:
        ost << "d";
        break;
    case Constant:
        ost << "c";
        break;
    }

    return ost;
}


gene_family_reconstructor::gene_family_reconstructor(std::ostream & ost, const lambda* lambda, double lambda_multiplier, const clade *p_tree,
    int max_family_size,
    int max_root_family_size,
    const gene_family *gf,
    matrix_cache *p_calc,
    root_equilibrium_distribution* p_prior) : _gene_family(gf), _p_calc(p_calc), _p_prior(p_prior), _lambda(lambda), _p_tree(p_tree),
    _max_family_size(max_family_size), _max_root_family_size(max_root_family_size), _lambda_multiplier(lambda_multiplier)
{
}

class child_multiplier
{
    clademap<std::vector<double> >& _L;
    double value;
    int _j;
public:
    bool write = false;
    child_multiplier(clademap<std::vector<double> >& L, int j) : _L(L), _j(j)
    {
        value = 1.0;
    }

    void operator()(const clade *c)
    {
        value *= _L[c][_j];
        if (write)
            cout << "Child factor " << c->get_taxon_name() << " = " << _L[c][_j] << endl;
    }

    double result() const {
        return value;
    }
};

void gene_family_reconstructor::reconstruct_leaf_node(const clade * c, lambda * _lambda)
{
    auto& C = all_node_Cs[c];
    auto& L = all_node_Ls[c];
    C.resize(_max_family_size + 1);
    L.resize(_max_family_size + 1);

    double branch_length = c->get_branch_length();

    L.resize(_max_family_size + 1);

    int observed_count = _gene_family->get_species_size(c->get_taxon_name());
    fill(C.begin(), C.end(), observed_count);

    auto matrix = _p_calc->get_matrix(branch_length, _lambda->get_value_for_clade(c));
    // i will be the parent size
    for (size_t i = 1; i < L.size(); ++i)
    {
        L[i] = matrix->get(i, observed_count);
    }
}

void gene_family_reconstructor::reconstruct_root_node(const clade * c)
{
    auto& L = all_node_Ls[c];
    auto& C = all_node_Cs[c];

    L.resize(min(_max_family_size, _max_root_family_size) + 1);
    // At the root, we pick a single reconstructed state (step 4 of Pupko)
    C.resize(1);

    // i is the parent, j is the child
    for (size_t i = 1; i < L.size(); ++i)
    {
        double max_val = -1;

        for (size_t j = 1; j < L.size(); ++j)
        {
            child_multiplier cr(all_node_Ls, j);
            c->apply_to_descendants(cr);
            double val = cr.result() * _p_prior->compute(j);
            if (val > max_val)
            {
                max_val = val;
                C[0] = j;
            }
        }

        L[i] = max_val;
    }

    
    if (*max_element(L.begin(), L.end()) == 0.0)
    {
        cerr << "WARNING: failed to calculate L value at root" << endl;
    }
}

void gene_family_reconstructor::reconstruct_internal_node(const clade * c, lambda * _lambda)
{
    auto& C = all_node_Cs[c];
    auto& L = all_node_Ls[c];
    C.resize(_max_family_size + 1);
    L.resize(_max_family_size + 1);

    double branch_length = c->get_branch_length();

    L.resize(_max_family_size + 1);

    auto matrix = _p_calc->get_matrix(branch_length, _lambda->get_value_for_clade(c));

    if (matrix->is_zero())
        throw runtime_error("Zero matrix found");
    // i is the parent, j is the child
    for (size_t i = 0; i < L.size(); ++i)
    {
        size_t max_j = 0;
        double max_val = -1;
        for (size_t j = 0; j < L.size(); ++j)
        {
            child_multiplier cr(all_node_Ls, j);
            c->apply_to_descendants(cr);
            double val = cr.result() * matrix->get(i,j);
            if (val > max_val)
            {
                max_j = j;
                max_val = val;
            }
        }

        L[i] = max_val;
        C[i] = max_j;
    }
}


void gene_family_reconstructor::operator()(const clade *c)
{
    // all_node_Cs and all_node_Ls are hashtables where the keys are nodes and values are vectors of doubles
    unique_ptr<lambda> ml(_lambda->multiply(_lambda_multiplier));

    if (c->is_leaf())
    {
        reconstruct_leaf_node(c, ml.get());
    }
    else if (c->is_root())
    {
        reconstruct_root_node(c);
    }
    else
    {
        reconstruct_internal_node(c, ml.get());
    }
}

class backtracker
{
    clademap<std::vector<int> >& _all_node_Cs;
    clademap<int>& reconstructed_states;
public:
    backtracker(clademap<int>& rc, clademap<std::vector<int> >& all_node_Cs, const clade *root) : 
        _all_node_Cs(all_node_Cs),
        reconstructed_states(rc)
    {
        reconstructed_states[root] = _all_node_Cs[root][0];
    }

    void operator()(clade *child)
    {
        if (!child->is_leaf())
        {
            auto& C = _all_node_Cs[child];
            int parent_c = reconstructed_states[child->get_parent()];
            reconstructed_states[child] = C[parent_c];
            child->apply_to_descendants(*this);
        }
    }
};

void gene_family_reconstructor::reconstruct()
{
    // Pupko's joint reconstruction algorithm
    _p_tree->apply_reverse_level_order(*this);

    backtracker b(reconstructed_states, all_node_Cs, _p_tree);
    _p_tree->apply_to_descendants(b);

    compute_increase_decrease(reconstructed_states, increase_decrease_map);
}

std::string gene_family_reconstructor::get_reconstructed_states(const clade *node) const
{
    int value = node->is_leaf() ? _gene_family->get_species_size(node->get_taxon_name()) : reconstructed_states.at(node);
    return to_string(value);
}

cladevector gene_family_reconstructor::get_taxa() const
{
    return _p_tree->find_internal_nodes();
}



void gene_family_reconstructor::print_reconstruction(std::ostream & ost, cladevector& order)
{
    auto f = [order, this](const clade *node) { 
        return newick_node(node, order, this); 
    };

    ost << "  TREE " << _gene_family->id() << " = ";
    _p_tree->write_newick(ost, f);

    ost << ';' << endl;
}

increase_decrease gene_family_reconstructor::get_increases_decreases(cladevector& order, double pvalue)
{
    increase_decrease result;
    result.change.resize(order.size());
    result.gene_family_id = _gene_family->id();
    result.pvalue = pvalue;

    transform(order.begin(), order.end(), result.change.begin(), [this](const clade *taxon)->family_size_change {
        return increase_decrease_map[taxon];
    });

    return result;
}

bool parent_compare(int a, int b)
{
    return a < b;
}

bool parent_compare(double a, double b)
{
    return parent_compare(int(std::round(a)), int(std::round(b)));
}

cladevector gene_family_reconstructor::get_nodes()
{
    cladevector result(reconstructed_states.size());
    std::transform(reconstructed_states.begin(), reconstructed_states.end(), result.begin(), [](std::pair<const clade *, int> v) { return v.first;  });
    return result;
}

template <typename T>
void compute_increase_decrease_t(clademap<T>& input, clademap<family_size_change>& output)
{
    for (auto &clade_state : input)
    {
        auto p_clade = clade_state.first;
        T size = clade_state.second;
        if (!p_clade->is_root())
        {
            T parent_size = input[p_clade->get_parent()];
            if (parent_compare(size, parent_size))
                output[p_clade] = Decrease;
            else if (parent_compare(parent_size, size))
                output[p_clade] = Increase;
            else
                output[p_clade] = Constant;
        }
    }
}

void compute_increase_decrease(clademap<int>& input, clademap<family_size_change>& output)
{
    compute_increase_decrease_t(input, output);
}

void compute_increase_decrease(clademap<double>& input, clademap<family_size_change>& output)
{
    compute_increase_decrease_t(input, output);
}

std::ostream& operator<<(std::ostream & ost, const increase_decrease& val)
{
    ost << val.gene_family_id << '\t';
    ost << val.pvalue << "\t";
    ost << (val.pvalue < 0.05 ? 'y' : 'n') << "\t";
    ostream_iterator<family_size_change> out_it(ost, "\t");
    copy(val.change.begin(), val.change.end(), out_it);

    if (!val.category_likelihoods.empty())
    {
        ostream_iterator<double> out_it2(ost, "\t");
        copy(val.category_likelihoods.begin(), val.category_likelihoods.end(), out_it2);
    }
    ost << endl;

    return ost;
}

void reconstruction::write_results(model *p_model, std::string output_prefix, std::vector<double>& pvalues)
{
    std::ofstream ofst(filename(p_model->name() + "_asr", output_prefix));
    print_reconstructed_states(ofst);

    std::ofstream family_results(filename(p_model->name() + "_family_results", output_prefix));
    print_increases_decreases_by_family(family_results, pvalues);

    std::ofstream clade_results(filename(p_model->name() + "_clade_results", output_prefix));
    print_increases_decreases_by_clade(clade_results);
}

