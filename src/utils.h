#ifndef utils_h
#define utils_h

#include <regex>
#include <string>
#include <iostream>
#include <fstream>

/* string tree_str = "((Sp_A:1.0,Sp_B:1.0):1,Sp_C:2);"; */
regex tokenizer("\\(|\\)|[^\\s\\(\\)\\:\\;\\,]+|\\:[+-]?[0-9]*\\.?[0-9]+([eE][+-]?[0-9]+)?|\\,|\\;"); // this cannot go inside the newick parser... ask Ben.

class newick_parser {

 public:
  string newick_string;
  int lp_count, rp_count;

  // two lines below dont compile if inside class... ask Ben.
  /* sregex_iterator regex_it(string newick_string.begin(), string newick_string.end(), tokenizer); */
  /* sregex_iterator regex_it_end; // an uninitialized sregex_iterator represents the ending position ! */

 /* methods */
  clade *parse_newick();
  clade *new_clade(clade *p_parent);
};

clade *newick_parser::parse_newick() {

  sregex_iterator regex_it(newick_string.begin(), newick_string.end(), tokenizer);
  sregex_iterator regex_it_end;
  clade *p_root_clade = new_clade(NULL);
  clade *p_current_clade = p_root_clade; // current_clade starts as the root
  
  // The first element below is empty b/c I initialized it in the class body
  for (; regex_it != regex_it_end; regex_it++) {
    /* cout << regex_it->str() << endl; */

    /* Start new clade */
    if (regex_it->str() == "(") {
      p_current_clade = new_clade(p_current_clade); // move down the tree (towards the present)
      // note that calling new_clade(some_clade) returns a new clade with some_clade as its parent
      lp_count++;
    }

    else if (regex_it->str() == ",") {
      /* if current clade is root */
      if (p_current_clade == p_root_clade) {
	cout << "Found root!" << endl;
  	p_root_clade = new_clade(NULL);
  	p_current_clade->p_parent = p_root_clade;
      }

      /* start new clade at same level as the current clade */
      p_current_clade = new_clade(p_current_clade->get_parent()); // move to the side of the tree
    }

    /* Finished current clade */
    else if (regex_it->str() == ")") {
      p_current_clade = p_current_clade->get_parent(); // move up the tree (into the past)
      rp_count++;
    }
    
    /* Finished newick string */
    else if (regex_it->str() == ";") { cout << "Finished reading string." << endl; break; }

    /* Reading branch length */
    else if (regex_it->str()[0] == ':') {
      /* This below is a string with just the branch length... gotta convert it to long... */
      /* p_current_clade->branch_length = regex_it->str().substr(1); */
    }
    
    /* Reading taxon name */
    else {
      /* cout << regex_it->str() << endl; */
      p_current_clade->taxon_name = regex_it->str();
    }
  }

  return p_root_clade;
}

clade *newick_parser::new_clade(clade *p_parent) {

  clade *p_new_clade = new clade();
  if (p_parent != NULL) {
    p_new_clade->p_parent = p_parent;
  }

  return p_new_clade;
}

#endif
