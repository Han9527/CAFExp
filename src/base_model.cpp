#include <cmath>
#include <numeric>
#include <iomanip>

#include "base_model.h"
#include "process.h"
#include "reconstruction_process.h"
#include "matrix_cache.h"

#include "root_equilibrium_distribution.h"

class base_lambda_optimizer : public optimizer
{
    clade *_p_tree;
    lambda *_p_lambda;
    base_model *_p_model;
    root_equilibrium_distribution *_p_distribution;
public:
    base_lambda_optimizer(clade *p_tree, lambda *p_lambda, base_model* p_model, root_equilibrium_distribution *p_distribution) :
        _p_tree(p_tree),
        _p_lambda(p_lambda),
        _p_model(p_model),
        _p_distribution(p_distribution)
    {
    }

    std::vector<double> initial_guesses();

    double calculate_score(double *values);

    virtual void finalize(double *results)
    {
        _p_lambda->update(results);
    }
};

class base_epsilon_optimizer : public optimizer
{
    error_model* _p_error_model;
    base_model *_p_model;
    root_equilibrium_distribution *_p_distribution;
    base_lambda_optimizer* _p_lambda_optimizer;

    std::vector<double> current_guesses;
public:
    base_epsilon_optimizer(base_model* p_model, 
        error_model *p_error_model, 
        root_equilibrium_distribution* p_distribution,
        base_lambda_optimizer* p_optimizer) :
        _p_model(p_model),
        _p_error_model(p_error_model),
        _p_distribution(p_distribution),
        _p_lambda_optimizer(p_optimizer)
    {

    }

    virtual ~base_epsilon_optimizer()
    {
        delete _p_lambda_optimizer;
    }

    virtual std::vector<double> initial_guesses()
    {
        current_guesses = _p_error_model->get_epsilons();
        return current_guesses;
    }

    virtual double calculate_score(double *values);

    virtual void finalize(double *results)
    {
        std::vector<double>().swap(current_guesses);    // required to convince CPPUtest that memory isn't being leaked
    }
};

base_model::base_model(lambda* p_lambda, clade *p_tree, vector<gene_family> *p_gene_families,
    int max_family_size, int max_root_family_size, std::map<int, int> * p_rootdist_map, error_model *p_error_model) :
    model(p_lambda, p_tree, p_gene_families, max_family_size, max_root_family_size, p_error_model)
{
    if (p_rootdist_map != NULL)
    {
        _rootdist_vec = vectorize_map(p_rootdist_map); // in vector form
        _total_n_families_sim = _rootdist_vec.size();
    }
}

base_model::~base_model()
{
    for (auto proc : processes)
        delete proc;

    for (auto rec_proc : _rec_processes)
        delete rec_proc;
}

simulation_process* base_model::create_simulation_process(int family_number) {
    return new simulation_process(_ost, _p_lambda, 1.0, _p_tree, _max_family_size, _max_root_family_size, _rootdist_vec, family_number); // if a single _lambda_multiplier, how do we do it?
}

reconstruction_process* base_model::create_reconstruction_process(int family_number, matrix_cache *p_calc, root_equilibrium_distribution* p_prior) {
    return new reconstruction_process(_ost, _p_lambda, 1.0, _p_tree, _max_family_size, _max_root_family_size, _rootdist_vec, 
        &_p_gene_families->at(family_number), p_calc, p_prior);
}

void base_model::start_inference_processes()
{
    for (auto proc : processes)
        delete proc;
    processes.clear();
    for (int i = 0; i < _p_gene_families->size(); ++i) {
        inference_process *p_new_process = new inference_process(_ost, _p_lambda, 1.0, _p_tree, _max_family_size, _max_root_family_size, &_p_gene_families->at(i), _rootdist_vec, _p_error_model); // if a single _lambda_multiplier, how do we do it?
        processes.push_back(p_new_process);
    }
}

double base_model::infer_processes(root_equilibrium_distribution *prior) {
    if (!_p_lambda->is_valid())
    {
#ifndef SILENT
        std::cout << "-lnL: " << log(0) << std::endl;
#endif
        return -log(0);
    }

    initialize_rootdist_if_necessary();
    prior->initialize(_rootdist_vec);

    results.resize(processes.size());
    std::vector<double> all_families_likelihood(processes.size());

    branch_length_finder lengths;
    _p_tree->apply_prefix_order(lengths);

    matrix_cache calc;
    calc.precalculate_matrices(_max_family_size + 1, get_lambda_values(_p_lambda), lengths.result());
    // prune all the families with the same lambda
#pragma omp parallel for

    for (int i = 0; i < processes.size(); ++i) {

        auto partial_likelihood = processes[i]->prune(calc);    // probabilities of various family sizes
        std::vector<double> full(partial_likelihood.size());

        for (size_t j = 0; j < partial_likelihood.size(); ++j) {
            double eq_freq = prior->compute(j);

            full[j] = std::log(partial_likelihood[j]) + std::log(eq_freq);
        }

        //        all_families_likelihood[i] = accumulate(full.begin(), full.end(), 0.0); // sum over all sizes (Felsenstein's approach)
        all_families_likelihood[i] = log(exp(*max_element(full.begin(), full.end()))); // get max (CAFE's approach)
       // cout << i << " contribution " << scientific << all_families_likelihood[i] << endl;
        results[i] = family_info_stash(i, 0.0, 0.0, 0.0, all_families_likelihood[i], false);
    }
    double final_likelihood = -std::accumulate(all_families_likelihood.begin(), all_families_likelihood.end(), 0.0); // sum over all families

    return final_likelihood;
}

void base_model::print_results(std::ostream& ost)
{
    ost << "#FamilyID\tLikelihood of Family" << endl;
    for (const auto& r : results)
    {
        ost << r.family_id << "\t" << r.posterior_probability << endl;
    }
}

optimizer *base_model::get_lambda_optimizer(root_equilibrium_distribution* p_distribution)
{
    return new base_lambda_optimizer(_p_tree, _p_lambda, this, p_distribution);
}

optimizer *base_model::get_epsilon_optimizer(root_equilibrium_distribution* p_distribution)
{
    auto lambda_optimizer = new base_lambda_optimizer(_p_tree, _p_lambda, this, p_distribution);
    lambda_optimizer->quiet = true;
    return new base_epsilon_optimizer(this, _p_error_model, p_distribution, lambda_optimizer);
}

void base_model::reconstruct_ancestral_states(matrix_cache *p_calc, root_equilibrium_distribution* p_prior)
{
    cout << "Starting reconstruction processes for base model" << endl;
    _rec_processes.clear();

    for (int i = 0; i < _p_gene_families->size(); ++i)
    {
        _rec_processes.push_back(create_reconstruction_process(i, p_calc, p_prior));
    }

    branch_length_finder lengths;
    _p_tree->apply_prefix_order(lengths);
    p_calc->precalculate_matrices(_max_family_size + 1, get_lambda_values(_p_lambda), lengths.result());

#ifndef SILENT
    cout << "Base: reconstructing ancestral states - lambda = " << *_p_lambda << endl;
#endif

    for (auto p : _rec_processes)
    {
        p->reconstruct();
    }
#ifndef SILENT
    cout << "Done!" << endl;
#endif
}

void base_model::print_reconstructed_states(std::ostream& ost) {
    auto rec = _rec_processes[0];
    auto order = rec->get_taxa();
    for (auto& it : order) {
        ost << "#" << it->get_taxon_name() << "\n";
    }

    for (int i = 0; i < _rec_processes.size(); ++i) {
        _rec_processes[i]->print_reconstruction(ost, order);
    }
}

std::vector<double> base_lambda_optimizer::initial_guesses()
{
    branch_length_finder finder;
    _p_tree->apply_prefix_order(finder);
    std::vector<double> result(_p_lambda->count());
    for (auto& i : result)
    {
        i = 1.0 / finder.longest() * unifrnd();
    }
    return result;
}

double base_lambda_optimizer::calculate_score(double *values)
{
    _p_lambda->update(values);

    if (!quiet)
        cout << "Lambda: " << *_p_lambda << std::endl;

    _p_model->start_inference_processes();

   double score = _p_model->infer_processes(_p_distribution);

   if (!quiet)
       std::cout << "Score (-lnL): " << setw(15) << setprecision(14) << score << std::endl;

   return score;
}

double base_epsilon_optimizer::calculate_score(double *values)
{
    map<double, double> replacements;
    for (size_t i = 0; i < current_guesses.size(); ++i)
        replacements[current_guesses[i]] = values[i];

    _p_error_model->replace_epsilons(&replacements);

    if (!quiet)
        cout << *values << "Error Model Run" << std::endl;

    _p_lambda_optimizer->optimize();

    _p_model->start_inference_processes();

    double score = _p_model->infer_processes(_p_distribution);

    if (!quiet)
    {
        std::cout << "Score with above error models: " << setw(15) << setprecision(14) << score << std::endl;
        std::cout << "Lambda with above error models: " << setw(15) << setprecision(14) << *_p_model->get_lambda() << std::endl;
    }

    return score;
}

