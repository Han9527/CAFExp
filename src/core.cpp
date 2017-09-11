#include <vector>
#include <iostream>
#include <valarray>
#include <fstream>
#include <random>
#include "clade.h"
#include "core.h"
#include "family_generator.h"
#include "gamma.h"

/* START: Drawing random root size from uniform */
template<typename itr, typename random_generator>
itr select_randomly(itr start, itr end, random_generator& g) {
    std::uniform_int_distribution<> dis(0, std::distance(start, end) - 1);
    std::advance(start, dis(g)); // advances iterator (start) by dis(g), where g is a seeded mt generator
    return start;
}

template<typename itr>
itr select_randomly(itr start, itr end) {
    static std::random_device rd; // randomly generates uniformly distributed ints (seed)
    static std::mt19937 gen(rd()); // seeding Mersenne Twister generator
    return select_randomly(start, end, gen); // plug in mt generator to advance our container and draw random element from it
}
/* END: Drawing random root size from uniform */

class process {
private:
    ostream & _ost;
    lambda* _lambda;
    double _lambda_multiplier;
    clade *_p_tree;
    int _max_family_size;
    int _max_root_family_size;
    int _max_family_size_sim;
    vector<int> _rootdist_vec;
    int _root_size; // will be drawn from _rootdist_vec by process itself
    trial *_my_simulation;
    gene_family *_p_gene_family;
    
public:
    process(): _ost(cout), _lambda(NULL), _lambda_multiplier(1.0) {}
    
    process(ostream & ost, lambda* lambda, double lambda_multiplier, clade *p_tree, int max_family_size, int max_root_family_size, int max_family_size_sim, vector<int> rootdist): _ost(ost), _lambda(lambda), _lambda_multiplier(lambda_multiplier), _p_tree(p_tree), _max_family_size(max_family_size), _max_root_family_size(max_root_family_size), _max_family_size_sim(max_family_size_sim), _rootdist_vec(rootdist) {
			
        // generating uniform root distribution when no distribution is provided 
	if (_rootdist_vec.empty()) {
            cout << "Max family size to simulate: " << _max_family_size_sim << endl;
            _rootdist_vec.resize(_max_family_size_sim);
			
            for (size_t i = 0; i < _rootdist_vec.size(); ++i)
		_rootdist_vec[i] = i;
        }

        _root_size = *select_randomly(_rootdist_vec.begin(), _rootdist_vec.end()); // getting a random root size from the provided (core's) root distribution
        cout << "_root_size is " << _root_size << endl;
    }
    

    process(ostream & ost, lambda* lambda, double lambda_multiplier, clade *p_tree, int max_family_size, int max_root_family_size, gene_family *fam, vector<int> rootdist) : _ost(ost), _lambda(lambda), _lambda_multiplier(lambda_multiplier), _p_tree(p_tree), _max_family_size(max_family_size), _max_root_family_size(max_root_family_size), _rootdist_vec(rootdist) {
		_p_gene_family = fam;
    }

    void run_simulation();

    void prune();
    
    void print_simulation(std::ostream & ost);
    
    trial * get_simulation();
};

//! Run process' simulation
void process::run_simulation() {
	single_lambda *sl = dynamic_cast<single_lambda*>(_lambda);	// we don't support multiple lambdas yet
	double lambda_m = sl->get_single_lambda() * _lambda_multiplier;
	_my_simulation = simulate_family_from_root_size(_p_tree, _root_size, _max_family_size_sim, lambda_m);
}

//! Prune process
void process::prune() {
    single_lambda *sl = dynamic_cast<single_lambda*>(_lambda);	// we don't support multiple lambdas yet
    
    cout << endl << "Max root family size is: " << _max_root_family_size << endl;
    cout << "Max family size is: " << _max_family_size << endl;
    cout << "Lambda multiplier is: " << _lambda_multiplier << endl;
    
    likelihood_computer pruner(_max_root_family_size, _max_family_size, sl->multiply(_lambda_multiplier), _p_gene_family); // likelihood_computer has a pointer to a gene family as a member, that's why &(*p_gene_families)[0]
    
    cout << "  About to prune process." << endl;
    _p_tree->apply_reverse_level_order(pruner);
    
    vector<double> partial_likelihood = pruner.get_likelihoods(_p_tree); // likelihood of the whole tree = multiplication of likelihood of all nodes
    
    int count = 1;
    for (std::vector<double>::iterator it = partial_likelihood.begin(); it != partial_likelihood.end(); ++it) {
        cout << "Likelihood " << count << ": " << *it << endl;
        count = count+1;
    }
}

//! Printing process' simulation
void process::print_simulation(std::ostream & ost) {

    // Printing gene counts
    for (trial::iterator it = _my_simulation->begin(); it != _my_simulation->end(); ++it) {
	ost << it->second << "\t";
    }

    ost << _lambda_multiplier << endl;
} 

//! Return simulation
trial * process::get_simulation() {
    return _my_simulation;
}

void gamma_bundle::prune() {
    for (int i = 0; i < processes.size(); ++i)
	processes[i]->prune();
}

//! Simulation: core constructor when just alpha is provided.
core::core(ostream & ost, lambda* lambda, clade *p_tree, int max_family_size, int total_n_families, vector<int> rootdist_vec,
	int n_gamma_cats, double alpha) : _ost(ost), _p_lambda(lambda), _p_tree(p_tree), _max_family_size(max_family_size), 
	_total_n_families_sim(total_n_families), _rootdist_vec(rootdist_vec), _gamma_cat_probs(n_gamma_cats),
	_lambda_multipliers(n_gamma_cats)
{
	if (!rootdist_vec.empty()) {
		_rootdist_bins.push_back(rootdist_vec); // just 1st element
	}

	else {
		_rootdist_vec = uniform_dist(total_n_families, 1, max_family_size); // the user did not specify one... using uniform from 1 to max_family_size!
		_rootdist_bins.push_back(_rootdist_vec); // just 1st element (but we could specify different root dists for each lambda bin)
	}

	if (n_gamma_cats > 1)
	{
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

core::core(ostream & ost, lambda* lambda, clade *p_tree, int max_family_size, int total_n_families, vector<int> rootdist_vec,
	vector<int>& cats, vector<double>&mul) : _ost(ost), _p_lambda(lambda), _p_tree(p_tree), _max_family_size(max_family_size),
	_total_n_families_sim(total_n_families), _rootdist_vec(rootdist_vec),
	_gamma_cats(cats), _lambda_multipliers(mul)
{
}

//! Set pointer to lambda in core class
void core::set_lambda(lambda *p_lambda) {
    _p_lambda = p_lambda;
}

//! Set pointer to lambda in core class
void core::set_tree(clade *p_tree) {
    _p_tree = p_tree;
}

//! Set pointer to vector of gene family class instances
void core::set_gene_families(std::vector<gene_family> *p_gene_families) {
    _p_gene_families = p_gene_families;
}

//! Set max family sizes and max root family sizes for INFERENCE
void core::set_max_sizes(int max_family_size, int max_root_family_size) {
    _max_family_size = max_family_size;
    _max_root_family_size = max_root_family_size;
}

//! Set max family sizes and max root family sizes
void core::set_max_size_sim(int max_family_size_sim) {
    _max_family_size_sim = max_family_size_sim;
}


//! Set root distribution vector
void core::set_rootdist_vec(std::vector<int> rootdist_vec) {
    _rootdist_vec = rootdist_vec;
}

//! Set total number of families to simulate
void core::set_total_n_families_sim(int total_n_families_sim) {
    _total_n_families_sim = total_n_families_sim;
}

//! Resize all gamma-related vectors according to provided number (integer) of gamma categories
void core::adjust_n_gamma_cats(int n_gamma_cats) {
    _gamma_cat_probs.resize(n_gamma_cats);
    _lambda_multipliers.resize(n_gamma_cats);
}

//! Resize gamma_cats vector that assigns gamma class membership of families to be inferred/simulated
void core::adjust_family_gamma_membership(int n_families) {
    _gamma_cats.resize(n_families);
}

//! Set alpha for gamma distribution
void core::set_alpha(double alpha) {
    _alpha = alpha;
    if (_gamma_cats.size() > 1)
	get_gamma(_gamma_cat_probs, _lambda_multipliers, alpha); // passing vectors by reference
}

//! Set lambda multipliers for each gamma category
void core::set_lambda_multipliers(std::vector<double> lambda_multipliers) {
    _lambda_multipliers = lambda_multipliers;
}

//! Set lambda bins (each int is a vector pointing to a gamma category)
void core::set_lambda_bins(std::vector<int> lambda_bins) {
    _gamma_cats = lambda_bins;
}

//! Populate _processes (vector of processes)
void core::start_sim_processes() {
    
    for (int i = 0; i < _total_n_families_sim; ++i) {
        double lambda_bin = _gamma_cats[i];      
        process *p_new_process = new process(_ost, _p_lambda, _lambda_multipliers[lambda_bin], _p_tree, _max_family_size, _max_root_family_size, _max_family_size_sim, _rootdist_vec); // if a single _lambda_multiplier, how do we do it?
        _sim_processes.push_back(p_new_process);
    }
    
    // cout << _sim_processes.size() << " processes have been started." << endl;
}

void core::start_inference_processes() {

    for (int i = 0; i < _p_gene_families->size(); ++i) {
	gamma_bundle bundle;
        
        cout << "Started inference bundle " << i+1 << endl;
	
        for (int j = 0; j < _gamma_cat_probs.size(); ++j) {
            double lambda_bin = _gamma_cat_probs[j];
            process *p_new_process = new process(_ost, _p_lambda, _lambda_multipliers[lambda_bin], _p_tree, _max_family_size, _max_root_family_size, &_p_gene_families->at(i), _rootdist_vec); // if a single _lambda_multiplier, how do we do it?
            bundle.add(p_new_process);
            
            cout << "  Started inference process " << j+1 << endl;
            
	}

	_inference_bundles.push_back(bundle);
    }
}


//! Run simulations in all processes, in series... (TODO: in parallel!)
void core::simulate_processes() {
    for (int i  = 0; i < _total_n_families_sim; ++i) {
        _sim_processes[i]->run_simulation();
    }
}

//! Infer bundle
void core::infer_processes() {
    for (int i = 0; i < _inference_bundles.size(); ++i) {
        cout << endl << "About to prune a gamma bundle." << endl;
	_inference_bundles[i].prune();
    }
}

//! Print processes' simulations
void core::adjust_family(std::ostream& ost) {
    
    // Printing header
    for (trial::iterator it = _sim_processes[0]->get_simulation()->begin(); it != _sim_processes[0]->get_simulation()->end(); ++it) {
	ost << "#" << it->first->get_taxon_name() << endl;
    }
    
    for (int i = 0; i < _total_n_families_sim; ++i) {
        _sim_processes[i]->print_simulation(cout);
    }
}

/* TODO: later this will become a member of the core class, which is the wrapper of the process class*/
void core::print_parameter_values() {
    
    cout << endl << "You have set the following parameter values:" << endl;
    
    if (dynamic_cast<single_lambda*>(_p_lambda)->get_single_lambda() == 0.0) {
        cout << "Lambda has not been specified." << endl;
    }
    else {
        cout << "Lambda: " << _p_lambda << endl;
    }
    
    if (_p_tree == NULL) {
        cout << "A tree has not been specified." << endl;
    }
    else {
        cout << "The tree is:" << endl;
        _p_tree->print_clade();
    }
    
}
