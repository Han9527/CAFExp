#include <algorithm>
#include <cassert>
#include <iostream>
#include <numeric>
#include <functional>
#include <cmath>

#include "gamma_bundle.h"
#include "reconstruction_process.h"
#include "root_equilibrium_distribution.h"
#include "clade.h"

using namespace std;

gamma_bundle::gamma_bundle(inference_process_factory& factory, std::vector<double> lambda_multipliers)
{
    std::transform(lambda_multipliers.begin(), lambda_multipliers.end(), std::back_inserter(_inf_processes), factory);
    for (auto p : _inf_processes)
    {
        _rec_processes.push_back(factory.create_reconstruction_process(p->get_lambda_multiplier()));
    }
}

gamma_bundle::~gamma_bundle()
{
    for (auto i : _inf_processes)
        delete i;

    for (auto r : _rec_processes)
        delete r;
}

std::vector<clade *> gamma_bundle::get_taxa()
{
    return _rec_processes[0]->get_taxa();
}

void gamma_bundle::clear()
{
    for (size_t i = 0; i < _inf_processes.size(); ++i)
    {
        delete _inf_processes[i];
    }
    _inf_processes.clear();
}

void gamma_bundle::set_values(matrix_cache *calc, root_equilibrium_distribution*prior)
{
    for (auto rec : _rec_processes)
    {
        rec->set_values(calc, prior);
    }
}

bool gamma_bundle::prune(const vector<double>& _gamma_cat_probs, root_equilibrium_distribution *eq, matrix_cache& calc) {
    assert(_gamma_cat_probs.size() == _inf_processes.size());
    _category_likelihoods.clear();

    for (int k = 0; k < _gamma_cat_probs.size(); ++k)
    {
        auto partial_likelihood = _inf_processes[k]->prune(calc);
        if (accumulate(partial_likelihood.begin(), partial_likelihood.end(), 0.0) == 0.0)
            return false;   // saturation

        std::vector<double> full(partial_likelihood.size());
        for (size_t j = 0; j < partial_likelihood.size(); ++j) {
            double eq_freq = eq->compute(j);
            full[j] = partial_likelihood[j] * eq_freq;
        }

//        _category_likelihoods.push_back(accumulate(full.begin(), full.end(), 0.0) * _gamma_cat_probs[k]); // sum over all sizes (Felsenstein's approach)
        _category_likelihoods.push_back(*max_element(full.begin(), full.end()) * _gamma_cat_probs[k]); // get max (CAFE's approach)
    }

    return true;
}

void gamma_bundle::reconstruct(const vector<double>& _gamma_cat_probs)
{
    for (int k = 0; k < _gamma_cat_probs.size(); ++k)
    {
        _rec_processes[k]->reconstruct();
    }

    // multiply every reconstruction by gamma_cat_prob
    reconstruction = reconstruction_process::get_weighted_averages(_rec_processes, _gamma_cat_probs);

    compute_increase_decrease(reconstruction, increase_decrease_map);
}

void gamma_bundle::print_reconstruction(std::ostream& ost, std::vector<clade *> order)
{
    ost << _rec_processes[0]->get_family_id() << '\t';
    std::function<std::string()> f;
    for (auto proc : _rec_processes)
    {
        auto s = proc->get_reconstructed_states();
        f = [&]() {f = []() { return "-"; }; return ""; };;
        for (auto taxon : order)
            ost << f() << s[taxon];
        ost << '\t';
    }

    f = [&]() {f = []() { return "-"; }; return ""; };
    for (auto taxon : order)
        ost << f() << std::round(reconstruction[taxon]);

    ost << endl;
}

gamma_increase_decrease gamma_bundle::get_increases_decreases(std::vector<clade *>& order, double pvalue)
{
    gamma_increase_decrease result;
    result.change.resize(order.size());
    result.gene_family_id = _rec_processes[0]->get_family_id();

    transform(order.begin(), order.end(), result.change.begin(), [this](clade *taxon)->family_size_change {
        return taxon->is_root() ? Constant : increase_decrease_map.at(taxon);
    });

    result.category_likelihoods = _category_likelihoods;
    return result;
}

std::ostream& operator<<(std::ostream & ost, const gamma_increase_decrease& val)
{
    ost << val.gene_family_id << '\t';
    ostream_iterator<family_size_change> out_it(ost, "\t");
    copy(val.change.begin(), val.change.end(), out_it);

    ostream_iterator<double> out_it2(ost, "\t");
    copy(val.category_likelihoods.begin(), val.category_likelihoods.end(), out_it2);

    ost << endl;
    return ost;
}

double gamma_bundle::get_lambda_likelihood(int family_id)
{
    return _inf_processes[family_id]->get_lambda_multiplier();
}

inference_process_factory::inference_process_factory(std::ostream & ost, lambda* lambda, clade *p_tree, int max_family_size,
        int max_root_family_size, std::vector<int> rootdist) :
        _ost(ost), _lambda(lambda), _p_tree(p_tree),
        _max_family_size(max_family_size), _max_root_family_size(max_root_family_size),
        _rootdist_vec(rootdist), _family(NULL)
{

}

inference_process* inference_process_factory::operator()(double lambda_multiplier)
{
    return new inference_process(_ost, _lambda, lambda_multiplier, _p_tree, _max_family_size, _max_root_family_size, _family, _rootdist_vec, NULL); // if a single _lambda_multiplier, how do we do it?
}

reconstruction_process* inference_process_factory::create_reconstruction_process(double lambda_multiplier)
{
    return new reconstruction_process(_ost, _lambda, lambda_multiplier, _p_tree, _max_family_size, _max_root_family_size, _rootdist_vec, _family,
        NULL, NULL);
}
