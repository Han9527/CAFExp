#include <assert.h>
#include <numeric>
#include <iomanip>

#include "gamma_core.h"
#include "gamma.h"
#include "process.h"
#include "root_equilibrium_distribution.h"

class inference_process_factory
{
    std::ostream & _ost;
    lambda* _lambda;
    double _lambda_multiplier;
    clade *_p_tree;
    int _max_family_size;
    int _max_root_family_size;
    std::vector<int> _rootdist_vec; // distribution of relative values. probability can be found by dividing a single value by the total of all values
    int _root_size; // will be drawn from _rootdist_vec by process itself
    gene_family *_family;
public:

    inference_process_factory(std::ostream & ost, lambda* lambda, clade *p_tree, int max_family_size,
        int max_root_family_size, std::vector<int> rootdist) :
        _ost(ost), _lambda(lambda), _p_tree(p_tree),
        _max_family_size(max_family_size), _max_root_family_size(max_root_family_size),
        _rootdist_vec(rootdist), _family(NULL)
    {

    }

    void set_gene_family(gene_family *family) {
        _family = family;
    }

    inference_process* operator()(double lambda_multiplier)
    {
        return new inference_process(_ost, _lambda, lambda_multiplier, _p_tree, _max_family_size, _max_root_family_size, _family, _rootdist_vec); // if a single _lambda_multiplier, how do we do it?
    }
};

gamma_model::gamma_model(lambda* p_lambda, clade *p_tree, std::vector<gene_family>* p_gene_families, int max_family_size,
    int max_root_family_size, int n_gamma_cats, double fixed_alpha, std::map<int, int> *p_rootdist_map) :
    model(p_lambda, p_tree, p_gene_families, max_family_size, max_root_family_size) {
    if (p_rootdist_map != NULL)
        _rootdist_vec = vectorize_map(p_rootdist_map); // in vector form
    
    this->initialize_with_alpha(n_gamma_cats, _rootdist_vec.size(), fixed_alpha);
    _total_n_families_sim = _rootdist_vec.size();
}

//! Simulation: gamma_model constructor when just alpha is provided.
gamma_model::gamma_model(ostream & ost, lambda* lambda, clade *p_tree, int max_family_size, int total_n_families, vector<int> rootdist_vec,
    int n_gamma_cats, double alpha) : model(ost, lambda, p_tree, max_family_size, total_n_families, rootdist_vec),
    _gamma_cat_probs(n_gamma_cats), _lambda_multipliers(n_gamma_cats) {

    if (!rootdist_vec.empty()) {
        _rootdist_bins.push_back(rootdist_vec); // just 1st element
    }

    else {
        _rootdist_vec = uniform_dist(total_n_families, 1, max_family_size); // the user did not specify one... using uniform from 1 to max_family_size!
        _rootdist_bins.push_back(_rootdist_vec); // just 1st element (but we could specify different root dists for each lambda bin)
    }

    if (n_gamma_cats > 1) {
        get_gamma(_gamma_cat_probs, _lambda_multipliers, alpha); // passing vectors by reference
        auto cats = weighted_cat_draw(total_n_families, _gamma_cat_probs);
        _gamma_cats = *cats;
        delete cats;
    }

    for (auto i = _gamma_cat_probs.begin(); i != _gamma_cat_probs.end(); ++i) {
        cout << "Should be all the same probability: " << *i << endl;
    }

    for (auto i = _lambda_multipliers.begin(); i != _lambda_multipliers.end(); ++i) {
        cout << "Lambda multiplier (rho) is: " << *i << endl;
    }

    for (auto i = _gamma_cats.begin(); i != _gamma_cats.end(); ++i) {
        cout << "Gamma category is: " << *i << endl;
    }

}

//! Simulation: model constructor when alpha is not provided and membership is provided directly, in addition to the lambda multipliers (something the user decides to use)
gamma_model::gamma_model(ostream & ost, lambda* lambda, clade *p_tree, int max_family_size, int total_n_families, vector<int> rootdist_vec,
    vector<int>& cats, vector<double>&mul) : model(ost, lambda, p_tree, max_family_size, total_n_families, rootdist_vec),
    _gamma_cats(cats), _lambda_multipliers(mul) {}

gamma_model::~gamma_model()
{
    for (size_t i = 0; i < _inference_bundles.size(); ++i)
        _inference_bundles[i].clear();
    _inference_bundles.clear();
}

void gamma_model::print_results(std::ostream& ost)
{
    ost << "#FamilyID\tGamma Cat Median\tLikelihood of Category\tLikelihood of Family\tPosterior Probability\tSignificant" << endl;

    std::ostream_iterator<family_info_stash> out_it(ost, "\n");
    std::copy(results.begin(), results.end(), out_it);
}


void gamma_model::initialize_with_alpha(int n_gamma_cats, int n_families, double alpha)
{
    adjust_n_gamma_cats(n_gamma_cats);
    adjust_family_gamma_membership(n_families);
    set_alpha(alpha, n_families);
}

void gamma_model::initialize_without_alpha(int n_gamma_cats, int n_families, vector<double> lambda_multipliers, std::vector<int> gamma_cats)
{
    assert(lambda_multipliers.size() == n_gamma_cats);
    assert(gamma_cats.size() == n_families);
    //    adjust_n_gamma_cats(n_gamma_cats);
    //    adjust_family_gamma_membership(n_families);
    //    set_alpha(alpha, n_families);

    set_lambda_multipliers(lambda_multipliers);
    _gamma_cats = gamma_cats;
}

//! Resize all gamma-related vectors according to provided number (integer) of gamma categories
void gamma_model::adjust_n_gamma_cats(int n_gamma_cats) {
    _gamma_cat_probs.resize(n_gamma_cats);
    _lambda_multipliers.resize(n_gamma_cats);
}

//! Resize gamma_cats vector that assigns gamma class membership of families to be inferred/simulated
void gamma_model::adjust_family_gamma_membership(int n_families) {
    _gamma_cats.resize(n_families);
}

//! Set alpha for gamma distribution
void gamma_model::set_alpha(double alpha, int n_families) {
    _alpha = alpha;
    if (_gamma_cats.size() > 1)
        get_gamma(_gamma_cat_probs, _lambda_multipliers, alpha); // passing vectors by reference

    vector<int>* cats = weighted_cat_draw(n_families, _gamma_cat_probs);
    _gamma_cats = *cats;
    delete cats;

//    for (std::vector<double>::iterator it = _gamma_cat_probs.begin(); it != _gamma_cat_probs.end(); ++it) {
//        cout << "Gamma cat prob is : " << *it << endl;
//    }
}

//! Set lambda multipliers for each gamma category
void gamma_model::set_lambda_multipliers(std::vector<double> lambda_multipliers) {
    _lambda_multipliers = lambda_multipliers;
}


void gamma_model::start_inference_processes() {

    _inference_bundles.clear();
    inference_process_factory factory(_ost, _p_lambda, _p_tree, _max_family_size, _max_root_family_size, _rootdist_vec);
    for (auto i = _p_gene_families->begin(); i != _p_gene_families->end(); ++i)
    {
        factory.set_gene_family(&(*i));
        _inference_bundles.push_back(gamma_bundle(factory, _lambda_multipliers));
    }
}

//! Populate _processes (vector of processes)
simulation_process* gamma_model::create_simulation_process(int family_number) {
    double lambda_bin = _gamma_cats[family_number];
    return new simulation_process(_ost, _p_lambda, _lambda_multipliers[lambda_bin], _p_tree, _max_family_size, _max_root_family_size, _rootdist_vec, family_number); // if a single _lambda_multiplier, how do we do it?
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

//! Infer bundle
double gamma_model::infer_processes(root_equilibrium_distribution *prior) {

    using namespace std;
    initialize_rootdist_if_necessary();

    prior->initialize(_rootdist_vec);
    vector<double> all_bundles_likelihood(_inference_bundles.size());

    for (int i = 0; i < _inference_bundles.size(); ++i) {
//        cout << endl << "About to prune a gamma bundle." << endl;
        gamma_bundle& bundle = _inference_bundles[i];

        vector<double> cat_likelihoods = bundle.prune(_gamma_cat_probs, prior);
        double family_likelihood = accumulate(cat_likelihoods.begin(), cat_likelihoods.end(), 0.0);

        vector<double> posterior_probabilities = get_posterior_probabilities(cat_likelihoods);

        for (size_t k = 0; k < cat_likelihoods.size(); ++k)
        {            
            results.push_back(family_info_stash(i, bundle.get_lambda_likelihood(k), cat_likelihoods[k], 
                family_likelihood, posterior_probabilities[k], posterior_probabilities[k] > 0.95));
//            cout << "Bundle " << i << " Process " << k << " family likelihood = " << family_likelihood << endl;
        }

        all_bundles_likelihood[i] = family_likelihood;
//        cout << "Bundle " << i << " family likelihood = " << family_likelihood << endl;

//        cout << "Likelihood of family " << i << " = " << all_bundles_likelihood[i] << endl;
    }

    double multi = accumulate(all_bundles_likelihood.begin(), all_bundles_likelihood.end(), 1.0, multiplies<double>());

//    cout << "Final answer: " << multi << endl;

    return multi;
}

std::vector<double> gamma_model::initial_guesses()
{
    max_branch_length_finder finder;
    _p_tree->apply_prefix_order(finder);
    double result = 1.0 / finder.result() * unifrnd();
    cout << "Initial lambda: " << result << std::endl;
    double alpha = unifrnd();
    return std::vector<double>{result, alpha};
}

void gamma_model::set_current_guesses(double *guesses)
{
    cout << "Attempting lambda: " << std::setw(15) << std::setprecision(14) << guesses[0] << ", alpha: " << guesses[1] << std::endl;
    probability_calculator*  calculator = new probability_calculator();
    single_lambda* lambda = new single_lambda(calculator, guesses[0]);
    set_lambda(lambda);

    double alpha = guesses[1];
    set_alpha(alpha, _p_gene_families->size());
}

gamma_bundle::gamma_bundle(inference_process_factory& factory, std::vector<double> lambda_multipliers)
{
    std::transform(lambda_multipliers.begin(), lambda_multipliers.end(), std::back_inserter(processes), factory);
}


void gamma_bundle::clear()
{
    for (size_t i = 0; i < processes.size(); ++i)
    {
        delete processes[i];
    }
    processes.clear();
}

std::vector<double> gamma_bundle::prune(const vector<double>& _gamma_cat_probs, root_equilibrium_distribution *eq) {
    assert(_gamma_cat_probs.size() == processes.size());

    std::vector<double> cat_likelihoods;

    for (int k = 0; k < _gamma_cat_probs.size(); ++k)
    {
        auto partial_likelihood = processes[k]->prune();
        std::vector<double> full(partial_likelihood.size());
        for (size_t j = 0; j < partial_likelihood.size(); ++j) {
            double eq_freq = eq->compute(j);
            full[j] = partial_likelihood[j] * eq_freq;
            // cout << "Likelihood " << j+1 << ": Partial " << partial_likelihood[j] << ", eq freq: " << eq_freq << ", Full " << full[j] << endl;
        }
        //all_gamma_cats_likelihood[k] = accumulate(full.begin(), full.end(), 0.0) * _gamma_cat_probs[k];
        //cout << "Likelihood of gamma cat " << k << " = " << all_gamma_cats_likelihood[k] << std::endl;

        cat_likelihoods.push_back(accumulate(full.begin(), full.end(), 0.0) * _gamma_cat_probs[k]);
    }

    return cat_likelihoods;
}

double gamma_bundle::get_lambda_likelihood(int family_id)
{
    return processes[family_id]->get_lambda_multiplier();
}

