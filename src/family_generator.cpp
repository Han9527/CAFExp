#include <vector>
#include <map>
#include <random>
#include "clade.h"
#include "probability.h"
#include "family_generator.h"
#include "matrix_cache.h"

std::default_random_engine gen(12);
std::uniform_real_distribution<> dis(0, 1); // draw random number from uniform distribution

//! Set the family size of a node to a random value, using parent's family size
/*!
  Starting from 0, the gene family size of the child (c) is increased until the cumulative probability of c (given the gene family size s of the parent) exceeds a random draw from a uniform distribution. When this happens, the last c becomes the child's gene family size.
 
  Note that the smaller the draw from the uniform, the higher the chance that c will be far away from s.

  The birth-death probability calculations are done using a class (probability_calculator defined in probability.h.
*/
void random_familysize_setter::operator()(clade *node) {

    if (node->is_root()) { return; } // if node is root, we do nothing

                                   /* Drawing random number from uniform */
                                   //  std::default_random_engine gen(static_cast<long unsigned int>(time(0)));
                                   // double rnd = dis(gen);
    double rnd = unifrnd();                          
    double cumul = 0;
    int parent_family_size = (*_p_tth_trial)[node->get_parent()];
    int c = 0; // c is the family size we will go to

    double lambda = _p_lambda->get_value_for_clade(node);

    if (parent_family_size > 0) {
        for (; c < _max_family_size - 1; c++) { // Ben: why -1
            double prob = _calculator->get_from_parent_fam_size_to_c(lambda, node->get_branch_length(), parent_family_size, c);
            cumul += prob;

            if (cumul >= rnd) {
                break;
            }
        }   
    }
    if (node->is_leaf() && _p_error_model != NULL)
    {
        if (c >= _p_error_model->get_max_count())
        {
            throw runtime_error("Trying to simulate leaf family size that was not included in error model");
        }
        auto probs = _p_error_model->get_probs(c);
       
        double rnd = unifrnd();
        if (rnd < probs[0])
        {
            c--;
        }
        else if (rnd > (1 - probs[2]))
        {
            c++;
        }
    }

    (*_p_tth_trial)[node] = c;
}

//! Simulate one gene family from a single root family size 
/*!
  Wraps around random_familysize_setter, which is the main engine of the simulator.
  Given a root gene family size, a lambda, simulates gene family counts for all nodes in the tree just once.
  Returns a trial, key = pointer to node, value = gene family size
*/
trial * simulate_family_from_root_size(clade *tree, int root_family_size, int max_family_size, lambda * p_lambda, error_model *p_error_model) {
    if (tree == NULL)
        throw runtime_error("No tree specified for simulation");

    matrix_cache calc;
    trial *result = new trial;
    random_familysize_setter rfs(result, max_family_size, p_lambda, &calc, p_error_model);
    (*result)[tree] = root_family_size;
    tree->apply_prefix_order(rfs); // this is where the () overload of random_familysize_setter is used
    
    return result;
}

//! Simulate gene families from a single root family size 
/*!
  Wraps around random_familysize_setter, which is the main engine of the simulator.
  Given a root gene family size, a lambda, and the number of trials, simulates gene family counts for all nodes in the tree (once per trial).
  Returns family_sizes map (= trial), key = pointer to node, value = gene family size
*/
//map<clade *, int> simulate_families_from_root_size(clade *tree, int num_trials, int root_family_size, int max_family_size, double lambda) {
vector<trial *> simulate_families_from_root_size(clade *tree, int num_trials, int root_family_size, int max_family_size, double lambda, error_model *p_error_model) {

    matrix_cache calc;
    vector<trial *> result;
    single_lambda lam(lambda);

    for (int t = 0; t < num_trials; ++t) {
        trial *p_tth_trial = new trial;
        random_familysize_setter rfs(p_tth_trial, max_family_size, &lam, &calc, p_error_model);
        (*p_tth_trial)[tree] = root_family_size;
        tree->apply_prefix_order(rfs); // this is where the () overload of random_familysize_setter is used
        //tree->apply_to_descendants(rfs); // setting the family size (random) of the descendants
        
        result.push_back(p_tth_trial);
    }
    
    return result;
}


//! Simulate gene family evolution from a distribution of root family sizes
/*!
  Wraps around simulate_families_from_root_size.
  Given a root family size distribution (provided as a map read using read_rootdist()), simulates gene family counts for all nodes in the tree, and the number of times specified by the distribution (e.g., 10 families with root size 2, 5 families with root size 3, etc.)
*/
//vector<vector<trial *> > simulate_families_from_distribution(clade *p_tree, int num_trials, const std::map<int, int>& root_dist, int max_family_size, double lambda) {
//    
//    vector<vector<trial *> > result;
//    std::map<int, int>::const_iterator it = root_dist.begin();
//    
//    while (it != root_dist.end()) {
//        int root_size = it->first;
//        cout << "Root size: " << root_size << endl;
//        int n_fams_this_size = it->second;
//        result.push_back(simulate_families_from_root_size(p_tree, n_fams_this_size, root_size, max_family_size, lambda, NULL));
//        it++;
//    }
//
//    return result;
//}
