#include <assert.h>
#include <numeric>
#include <iomanip>
#include <cmath>

#include "gamma_core.h"
#include "gamma.h"
#include "process.h"
#include "root_equilibrium_distribution.h"
#include "gene_family_reconstructor.h"
#include "matrix_cache.h"
#include "gamma_bundle.h"
#include "gene_family.h"
#include "user_data.h"
#include "optimizer_scorer.h"
#include "root_distribution.h"

gamma_model::gamma_model(lambda* p_lambda, clade *p_tree, std::vector<gene_family>* p_gene_families, int max_family_size,
    int max_root_family_size, int n_gamma_cats, double fixed_alpha, std::map<int, int> *p_rootdist_map, error_model* p_error_model) :
    model(p_lambda, p_tree, p_gene_families, max_family_size, max_root_family_size, p_error_model) {

    if (p_rootdist_map != NULL)
        _root_distribution.vectorize(*p_rootdist_map); // in vector form
    
    _gamma_cat_probs.resize(n_gamma_cats);
    _lambda_multipliers.resize(n_gamma_cats);
    set_alpha(fixed_alpha);
}

gamma_model::~gamma_model()
{
    for (auto f : _family_bundles)
    {
        f->clear();
        delete f;
    }
}

void gamma_model::write_vital_statistics(std::ostream& ost, double final_likelihood)
{
    model::write_vital_statistics(ost, final_likelihood);
    ost << "Alpha: " << _alpha << endl;
}

void gamma_model::write_family_likelihoods(std::ostream& ost)
{
    ost << "#FamilyID\tGamma Cat Median\tLikelihood of Category\tLikelihood of Family\tPosterior Probability\tSignificant" << endl;

    std::ostream_iterator<family_info_stash> out_it(ost, "\n");
    std::copy(results.begin(), results.end(), out_it);
}

//! Set alpha for gamma distribution
void gamma_model::set_alpha(double alpha) {

    _alpha = alpha;
    if (_gamma_cat_probs.size() > 1)
        get_gamma(_gamma_cat_probs, _lambda_multipliers, alpha); // passing vectors by reference

}

void gamma_model::write_probabilities(ostream& ost)
{
    ost << "Gamma cat probs are: ";
    for (double d : _gamma_cat_probs)
        ost << d << ",";
    ost << endl;

    ost << "Lambda multipliers are: ";
    for (double d : _lambda_multipliers)
        ost << d << ",";
    ost << endl;
}

void gamma_model::start_inference_processes(lambda *p_lambda) {

    _family_bundles.clear();
    inference_process_factory factory(_ost, p_lambda, _p_tree, _max_family_size, _max_root_family_size);
    for (auto i = _p_gene_families->begin(); i != _p_gene_families->end(); ++i)
    {
        factory.set_gene_family(&(*i));
        _family_bundles.push_back(new gamma_bundle(factory, _lambda_multipliers, _p_tree, &(*i)));
    }
}

void gamma_model::initialize_simulations(size_t count)
{
    if (_alpha <= 0)
    {
#ifndef SILENT
        cerr << "No alpha set for simulation. Setting randomly\n";
#endif
        set_alpha(unifrnd());
    }
    _gamma_cats.weighted_cat_draw(count, _gamma_cat_probs);
}

//! Populate _processes (vector of processes)
simulation_process* gamma_model::create_simulation_process(const user_data& data, const root_distribution& rootdist, int family_number) {
    double lambda_bin = _gamma_cats.draw(family_number);
    int max_family_size_sim;
    int root_size;

    if (data.rootdist.empty()) {
        max_family_size_sim = 100;
        root_size = rootdist.select_randomly(); // getting a random root size from the provided (core's) root distribution
    }
    else {
        max_family_size_sim = 2 * rootdist.max();
        root_size = rootdist.at(family_number);
    }


    return new simulation_process(_lambda_multipliers[lambda_bin], max_family_size_sim, root_size); 
}

std::vector<double> gamma_model::get_posterior_probabilities(std::vector<double> cat_likelihoods)
{
    size_t process_count = cat_likelihoods.size();

    vector<double> numerators(process_count);
    transform(cat_likelihoods.begin(), cat_likelihoods.end(), _gamma_cat_probs.begin(), numerators.begin(), multiplies<double>());

    double denominator = accumulate(numerators.begin(), numerators.end(), 0.0);
    vector<double> posterior_probabilities(process_count);
    transform(numerators.begin(), numerators.end(), posterior_probabilities.begin(), bind2nd(divides<double>(), denominator));

    return posterior_probabilities;
}

void gamma_model::prepare_matrices_for_simulation(matrix_cache& cache)
{
    branch_length_finder lengths;
    _p_tree->apply_prefix_order(lengths);
    //_lambda_multipliers
    for (auto multiplier : _lambda_multipliers)
    {
        unique_ptr<lambda> mult(_p_lambda->multiply(multiplier));
        cache.precalculate_matrices(get_lambda_values(mult.get()), lengths.result());
    }
}

//! Infer bundle
double gamma_model::infer_processes(root_equilibrium_distribution *prior) {

    if (!_p_lambda->is_valid())
    {
        std::cout << "-lnL: " << log(0) << std::endl;
        return -log(0);
    }
    if (_alpha < 0)
    {
        std::cout << "-lnL: " << log(0) << std::endl;
        return -log(0);
    }

    using namespace std;
    initialize_rootdist_if_necessary();

    prior->initialize(&_root_distribution);
    vector<double> all_bundles_likelihood(_family_bundles.size());

    bool success = true;
    matrix_cache calc(max(_max_root_family_size, _max_family_size) + 1);
    prepare_matrices_for_simulation(calc);

    vector<vector<family_info_stash>> pruning_results(_family_bundles.size());
#pragma omp parallel for
    for (int i = 0; i < _family_bundles.size(); ++i) {
        gamma_bundle* bundle = _family_bundles[i];

        if (bundle->prune(_gamma_cat_probs, prior, calc))
        {
            auto cat_likelihoods = bundle->get_category_likelihoods();
            double family_likelihood = accumulate(cat_likelihoods.begin(), cat_likelihoods.end(), 0.0);

            vector<double> posterior_probabilities = get_posterior_probabilities(cat_likelihoods);

            pruning_results[i].resize(cat_likelihoods.size());
            for (size_t k = 0; k < cat_likelihoods.size(); ++k)
            {
                pruning_results[i][k] = family_info_stash(bundle->get_family_id(), bundle->get_lambda_likelihood(k), cat_likelihoods[k],
                    family_likelihood, posterior_probabilities[k], posterior_probabilities[k] > 0.95);
                //            cout << "Bundle " << i << " Process " << k << " family likelihood = " << family_likelihood << endl;
            }
            all_bundles_likelihood[i] = std::log(family_likelihood);
        }
        else
        {
            // we got here because one of the gamma categories was saturated - reject this 
#pragma omp critical
            success = false;
        }
    }

    if (!success)
        return -log(0);

    for (auto& stashes : pruning_results)
    {
        for (auto& stash : stashes)
        {
            results.push_back(stash);
        }
    }
    double final_likelihood = -accumulate(all_bundles_likelihood.begin(), all_bundles_likelihood.end(), 0.0);

    std::cout << "-lnL: " << final_likelihood << std::endl;

    return final_likelihood;
}

optimizer_scorer *gamma_model::get_lambda_optimizer(user_data& data)
{
    bool estimate_lambda = data.p_lambda == NULL;
    bool estimate_alpha = _alpha <= 0.0;

    if (estimate_lambda && estimate_alpha)
    {
        initialize_lambda(data.p_lambda_tree);
        return new gamma_lambda_optimizer(_p_tree, _p_lambda, this, data.p_prior.get());
    }
    else if (estimate_lambda && !estimate_alpha)
    {
        initialize_lambda(data.p_lambda_tree);
        return new lambda_optimizer(_p_tree, _p_lambda, this, data.p_prior.get());
    }
    else if (!estimate_lambda && estimate_alpha)
    {
        _p_lambda = data.p_lambda->clone();
        return new gamma_optimizer(this, data.p_prior.get());
    }
    else
    {
        return nullptr;
    }
}

reconstruction* gamma_model::reconstruct_ancestral_states(matrix_cache *calc, root_equilibrium_distribution*prior)
{
    gamma_model_reconstruction* result = new gamma_model_reconstruction(_lambda_multipliers, _family_bundles);
    cout << "Gamma: reconstructing ancestral states - lambda = " << *_p_lambda << ", alpha = " << _alpha << endl;

    branch_length_finder lengths;
    _p_tree->apply_prefix_order(lengths);
    auto values = get_lambda_values(_p_lambda);
    vector<double> all;
    for (double multiplier : _lambda_multipliers)
    {
        for (double lambda : values)
        {
            all.push_back(lambda*multiplier);
        }
    }

    calc->precalculate_matrices(all, lengths.result());

#pragma omp parallel for
    for (size_t i = 0; i<_family_bundles.size(); ++i)
    {
        _family_bundles[i]->set_values(calc, prior);
        _family_bundles[i]->reconstruct(_gamma_cat_probs);
    }

    return result;
}

std::vector<double> gamma_lambda_optimizer::initial_guesses()
{
    double alpha = unifrnd();

    std::vector<double> x(_p_model->get_gamma_cat_probs_count());
    std::vector<double> y(_p_model->get_lambda_multiplier_count());
    get_gamma(x, y, alpha); // passing vectors by reference

    double largest_multiplier = *max_element(y.begin(), y.end());
    branch_length_finder finder;
    _p_tree->apply_prefix_order(finder);
    //double result = 1.0 / finder.result() * unifrnd();
    std::vector<double> lambdas(_p_lambda->count());
    const double longest_branch = finder.longest();
    generate(lambdas.begin(), lambdas.end(), [longest_branch, largest_multiplier] { return 1.0 / (longest_branch*largest_multiplier) * unifrnd(); });

    lambdas.push_back(alpha);
    return lambdas;

}

double gamma_lambda_optimizer::calculate_score(double *values)
{
    _p_lambda->update(values);

    double alpha = values[_p_lambda->count()];
    _p_model->set_alpha(alpha);

    cout << "Attempting lambda: " << *_p_lambda << ", alpha: " << alpha << std::endl;
    _p_model->write_probabilities(cout);

    _p_model->start_inference_processes(_p_lambda);

    return _p_model->infer_processes(_p_distribution);
}


void gamma_model_reconstruction::print_reconstructed_states(std::ostream& ost)
{
    if (_family_bundles.empty())
        return;

    auto rec = _family_bundles[0];
    auto order = rec->get_taxa();

    ost << "#NEXUS\nBEGIN TREES;\n";
    for (auto item : _family_bundles)
    {
        item->print_reconstruction(ost, order);
    }
    ost << "END;\n\n";

    ost << "BEGIN LAMBDA_MULTIPLIERS;\n";
    for (auto& lm : _lambda_multipliers)
    {
        ost << "  " << lm << ";\n";
    }
    ost << "END;\n";
    ost << endl;
}

void gamma_model_reconstruction::print_increases_decreases_by_family(std::ostream& ost, const std::vector<double>& pvalues)
{
    ::print_increases_decreases_by_family(ost, _family_bundles, pvalues);
}

void gamma_model_reconstruction::print_increases_decreases_by_clade(std::ostream& ost)
{
    ::print_increases_decreases_by_clade(ost, _family_bundles);
}

