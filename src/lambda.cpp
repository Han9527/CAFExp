#include <vector>
#include <map>
#include <cmath>
#include "lambda.h"
#include "clade.h"
#include "fminsearch.h"
#include "utils.h"
#include "probability.h"

using namespace std;

/* START: Holding lambda values and specifying how likelihood is computed depending on the number of different lambdas */

std::vector<double> single_lambda::calculate_child_factor(clade *child, std::size_t sz, std::vector<double> probabilities)
{
	std::vector<std::vector<double> > matrix = get_matrix(sz, child->get_branch_length(), _lambda); // Ben: is _factors[child].size() the same as _max_root_family_size? If so, why not use _max_root_family_size instead?
	return matrix_multiply(matrix, probabilities);
}

std::vector<double> multiple_lambda::calculate_child_factor(clade *child, std::size_t sz, std::vector<double> probabilities)
{
	std::string nodename = child->get_taxon_name();
	int lambda_index = _node_name_to_lambda_index[nodename];
	double lambda = _lambdas[lambda_index];
	cout << "Matrix for " << child->get_taxon_name() << endl;
	std::vector<std::vector<double> > matrix = get_matrix(sz, child->get_branch_length(), lambda); // Ben: is _factors[child].size() the same as _max_root_family_size? If so, why not use _max_root_family_size instead?
	return matrix_multiply(matrix, probabilities);
}

/* END: Holding lambda values and specifying how likelihood is computed depending on the number of different lambdas */

/// score of a lambda is the -log likelihood of the most likely resulting family size
double calculate_lambda_score(double* plambda, void* args)
{
	lambda_search_params *param = (lambda_search_params *)args;

	vector<double> posterior = get_posterior(param->families, param->max_family_size, *plambda, param->ptree);
	return -log(*max_element(posterior.begin(), posterior.end()));
}


double find_best_lambda(lambda_search_params *params)
{
	int lambda_len = 1;
	FMinSearch* pfm;
	pfm = fminsearch_new_with_eq(calculate_lambda_score, lambda_len, &params);
	pfm->tolx = 1e-6;
	pfm->tolf = 1e-6;
	double result;
	fminsearch_min(pfm, &result);
	double *re = fminsearch_get_minX(pfm);
	return *re;
}
