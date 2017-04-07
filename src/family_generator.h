
#include <vector>
#include <iosfwd>
#include <map>

class clade;

typedef std::map<clade *, int> trial;

trial simulate_families_from_root_size(clade *tree, int num_trials, int root_family_size, int max_family_size, double lambda);
vector<trial> simulate_families_from_distribution(clade *tree, int num_trials, const std::map<int, int>& root_dist, int max_family_size, double lambda);
void print_simulation(trial &sim, std::ostream& ost);
void print_simulation(std::vector<trial> &sim, std::ostream& ost);

