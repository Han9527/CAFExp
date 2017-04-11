#include "clade.h"

/* Recursive destructor */
clade::~clade() {

  for (desc_it = descendants.begin(), desc_end = descendants.end(); desc_it != desc_end; desc_it++) {
    delete *desc_it; // desc_it is a pointer, *desc_it is a clade; this statement calls the destructor and makes it recursive
  }
}

/* Returns pointer to parent */
clade *clade::get_parent() {

  return p_parent; // memory address
}

/* Adds descendant to vector of descendants */
void clade::add_descendant(clade *p_descendant) {
	
  descendants.push_back(p_descendant);
  name_interior_clade();
  if (!is_root()) {
    p_parent->name_interior_clade();
  }
}

/* Recursively fills vector of names provided as argument */
void clade::add_leaf_names(vector<string> &vector_names) {

  if (descendants.empty()) {
    vector_names.push_back(taxon_name); // base case (leaf), and it starts returning
  }

  else {
    for (int i = 0; i < descendants.size(); ++i) {
	descendants[i]->add_leaf_names(vector_names);
    }
  }
}

vector<clade*> clade::find_internal_nodes() {

  vector<clade*> internal_nodes;

  /* Base case: returns empty vector */
  if (is_leaf()) { return internal_nodes; }

  else {
	  internal_nodes.push_back(this);
    for (desc_it = descendants.begin(), desc_end = descendants.end(); desc_it != desc_end; desc_it++) {
      vector<clade*> descendant = (*desc_it)->find_internal_nodes(); // recursion
	  if (!descendant.empty()) { internal_nodes.insert(internal_nodes.end(), descendant.begin(), descendant.end()); }
    }

    return internal_nodes;
  }
}

class descendant_finder
{
  string _target;
  clade *result;
public:
  descendant_finder(string target) : _target(target), result(NULL)
  {

  }
  void operator()(clade *clade)
  {
    if (clade->get_taxon_name() == _target)
    {
      result = clade;
    }
  }
  clade *get_result() { return result;  }
};

/* Recursively find pointer to clade with provided taxon name */
clade *clade::find_descendant(string some_taxon_name) {
  descendant_finder finder(some_taxon_name);
  apply_prefix_order(finder);
  return finder.get_result();
#if 0
  cout << "Searching for descendant " << some_taxon_name << " in " << get_taxon_name() << endl;

  /* Base case: found some_taxon name and is not root */
  if (taxon_name == some_taxon_name) { return this; }

  /* If reached (wrong) leaf */
  else if (is_leaf()) { return NULL; }

  else {
    for (desc_it = descendants.begin(), desc_end = descendants.end(); desc_it != desc_end; desc_it++) {
    clade *descendant_to_find = (*desc_it)->find_descendant(some_taxon_name); // recursion

    if (descendant_to_find != NULL) { return descendant_to_find; } // recursion is only manifested if finds provided taxon name

    return NULL; // otherwise returns NULL
    }
  }
#endif
}
  
/* Finds branch length of clade with provided taxon name. Does so by calling find_descendant(), which recursively searches the tree */
double clade::find_branch_length(string some_taxon_name) {

  clade *clade = find_descendant(some_taxon_name);
  if (clade == NULL || clade->is_root()) { return 0; } // guarding against root query

  cout << "Found matching clade" << endl;
  return clade->branch_length;
}

void clade::name_interior_clade() {

  vector<string> descendant_names; // vector of names
  add_leaf_names(descendant_names); // fills vector of names
  sort(descendant_names.begin(), descendant_names.end()); // sorts alphabetically (from std)
  taxon_name.clear(); // resets whatever taxon_name was
  for (int i = 0; i < descendant_names.size(); ++i) {
    taxon_name += descendant_names[i];
  }
  if (p_parent)
    p_parent->name_interior_clade();
}

/* Prints names of immediate descendants */
void clade::print_immediate_descendants() {

  cout << "Me: " << taxon_name << " | Descendants: ";
  for (desc_it = descendants.begin(), desc_end = descendants.end(); desc_it != desc_end; desc_it++) {
    cout << (*desc_it)->taxon_name << " ";
  }
  
  cout << endl;
}

/* Recursively prints clade */
void clade::print_clade() {

  int level = 0;
  clade *p_ancestor = get_parent();
  while (p_ancestor)
  {
    level++;
    p_ancestor = p_ancestor->get_parent();
  }
  string blanks(level, ' ');

  cout << blanks << "My name is: " << taxon_name << endl;

  /* Base case: it is a leaf */
  if (descendants.empty()) { return; }
  
  for (desc_it = descendants.begin(), desc_end = descendants.end(); desc_it != desc_end ; desc_it++) {
    (*desc_it)->print_clade();
  }
}

bool clade::is_leaf() {

  return descendants.empty();
}

bool clade::is_root() {

  return get_parent() == NULL;
}

/* Testing implementation of clade class */

/* creating (((A,B)AB,C)ABC) 
clade *p_child11 = new clade("A", 1); // (leaf) child11 = A
clade *p_child12 = new clade("B", 1); // (leaf) child12 = B
clade *p_child1 = new clade("AB", 1); // (internal) child1 = AB
p_child1->add_descendant(p_child11);
p_child1->add_descendant(p_child12);
clade *p_child2 = new clade("C", 2); // (leaf) child2 = C
clade parent;
parent.taxon_name = "ABC"; // (root) parent = ABC
parent.add_descendant(p_child1);
parent.add_descendant(p_child2);
*/

/* testing print_immediate_descendants()
parent.print_immediate_descendants();
p_child1->print_immediate_descendants();
*/

/* testing print_clade()
parent.print_clade();
*/

/* testing is_leaf()
if (!parent.am_leaf()) { cout << "I am not leaf\n"; }
if (p_child11->am_leaf()) { cout << "I am leaf\n"; }
*/
