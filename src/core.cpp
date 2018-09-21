#include <vector>
#include <iostream>
#include <valarray>
#include <fstream>
#include <assert.h>
#include <numeric>

#include "clade.h"
#include "core.h"
#include "base_model.h"
#include "gamma_core.h"
#include "family_generator.h"
#include "process.h"
#include "poisson.h"
#include "root_equilibrium_distribution.h"
#include "gene_family_reconstructor.h"
#include "user_data.h"

std::vector<model *> build_models(const input_parameters& my_input_parameters, user_data& user_data) {

    model *p_model = NULL;

    std::vector<gene_family> *p_gene_families = &user_data.gene_families;

    if (my_input_parameters.is_simulating) {
        p_gene_families = NULL;
    }

    if (my_input_parameters.n_gamma_cats > 1)
    {
        auto gmodel = new gamma_model(user_data.p_lambda, user_data.p_tree, &user_data.gene_families, user_data.max_family_size, user_data.max_root_family_size,
            my_input_parameters.n_gamma_cats, my_input_parameters.fixed_alpha, &user_data.rootdist, user_data.p_error_model);
        gmodel->write_probabilities(cout);
        p_model = gmodel;
    }
    else
    {
        p_model = new base_model(user_data.p_lambda, user_data.p_tree, p_gene_families, user_data.max_family_size, user_data.max_root_family_size, &user_data.rootdist, user_data.p_error_model);
    }

    return std::vector<model *>{p_model};
}

std::ostream& operator<<(std::ostream& ost, const family_info_stash& r)
{
    ost << r.family_id << "\t" << r.lambda_multiplier << "\t" << r.category_likelihood << "\t" << r.family_likelihood;
    ost << "\t" << r.posterior_probability << "\t" << (r.significant ? "*" : "N/S");
    return ost;
}

model::model(lambda* p_lambda,
    const clade *p_tree,
    const vector<gene_family> *p_gene_families,
    int max_family_size,
    int max_root_family_size,
    error_model *p_error_model) :
    _ost(cout), _p_lambda(p_lambda), _p_tree(p_tree), _p_gene_families(p_gene_families), _max_family_size(max_family_size),
    _max_root_family_size(max_root_family_size), _p_error_model(p_error_model) 
{
    if (_p_gene_families)
        references = build_reference_list(*_p_gene_families);
}

//! Set max family sizes and max root family sizes for INFERENCE
void model::set_max_sizes(int max_family_size, int max_root_family_size) {
    _max_family_size = max_family_size;
    _max_root_family_size = max_root_family_size;
}

//! Set root distribution vector
void model::set_rootdist_vec(std::vector<int> rootdist_vec) {
    _rootdist_vec = rootdist_vec;
}

//! Set total number of families to simulate
void model::set_total_n_families_sim(int total_n_families_sim) {
    _total_n_families_sim = total_n_families_sim;
}

void model::start_sim_processes() {

    for (int i = 0; i < _total_n_families_sim; ++i) {
        _sim_processes.push_back(create_simulation_process(i));
    }

    // cout << _sim_processes.size() << " processes have been started." << endl;
}

//! Run simulations in all processes, in series... (TODO: in parallel!)
void model::simulate_processes() {
    for (int i = 0; i < _total_n_families_sim; ++i) {
        _sim_processes[i]->run_simulation();
    }
}

void model::print_processes(std::ostream& ost) {

    if (_sim_processes.empty())
    {
        cerr << "No simulations created" << endl;
        return;
    }
    trial *sim = _sim_processes[0]->get_simulation();
    for (trial::iterator it = sim->begin(); it != sim->end(); ++it) {
          ost << "#" << it->first->get_taxon_name() << "\n";
    }

    for (int i = 0; i < _total_n_families_sim; ++i) {
        _sim_processes[i]->print_simulation(ost, i);
    }
}

void model::initialize_rootdist_if_necessary()
{
    if (_rootdist_vec.empty())
    {
        _rootdist_vec.resize(_max_root_family_size);
        std::fill(_rootdist_vec.begin(), _rootdist_vec.end(), 1);
    }

}

//! Print processes' simulations
void model::adjust_family(std::ostream& ost) {
    
    // Printing header
    for (trial::iterator it = _sim_processes[0]->get_simulation()->begin(); it != _sim_processes[0]->get_simulation()->end(); ++it) {
	ost << "#" << it->first->get_taxon_name() << endl;
    }
    
    for (int i = 0; i < _total_n_families_sim; ++i) {
        _sim_processes[i]->print_simulation(cout, i);
    }
}

class lambda_counter
{
public:
    std::set<int> unique_lambdas;
    void operator()(const clade *p_node)
    {
        unique_lambdas.insert(p_node->get_lambda_index());
    }
};


void model::initialize_lambda(clade *p_lambda_tree)
{
    lambda *p_lambda = NULL;
    if (p_lambda_tree != NULL)
    {
        lambda_counter counter;
        p_lambda_tree->apply_prefix_order(counter);
        auto node_name_to_lambda_index = p_lambda_tree->get_lambda_index_map();
        p_lambda = new multiple_lambda(node_name_to_lambda_index, std::vector<double>(counter.unique_lambdas.size()));
        cout << "Searching for " << counter.unique_lambdas.size() << " lambdas" << endl;
    }
    else
    {
        p_lambda = new single_lambda(0.0);
    }

    _p_lambda = p_lambda;
}

class depth_finder
{
    clademap<double>& _depths;
    double _depth;
public:
    depth_finder(clademap<double>& depths, double depth) : _depths(depths), _depth(depth) {}
    void operator()(const clade *c)
    {
        _depths[c] = _depth + c->get_branch_length();
        depth_finder dp(_depths, _depth + c->get_branch_length());
        c->apply_to_descendants(dp);
    }
};

void model::print_node_depths(std::ostream& ost)
{
    clademap<double> depths;
    depth_finder dp(depths, 0);
    dp(_p_tree);
    
    double max_depth = 0;
    for (auto& x : depths)
    {
        if (x.second > max_depth)
            max_depth = x.second;
    }

    for (auto& x : depths)
    {
        if (!x.first->is_leaf())
        {
            ost << x.first->get_taxon_name() << "\t" << (max_depth - x.second) << endl;
        }
    }
}

void model::write_vital_statistics(std::ostream& ost, double final_likelihood)
{
    ost << "Model " << name() << " Result: " << final_likelihood << endl;
    ost << "Lambda: " << *get_lambda() << endl;
    if (_p_error_model)
        ost << "Epsilon: " << _p_error_model->get_epsilons()[0] << endl;
}

void branch_length_finder::operator()(const clade *c)
{
    _result.insert(c->get_branch_length());
}

double branch_length_finder::longest() const
{
    return *max_element(_result.begin(), _result.end());
}
