/**
 * @file exact_max_ip.cc
 * @author Parikshit Ram
 *
 * This file implements the functions declared 
 * for the class MaxIP.
 */

#include "exact_max_ip.h"

//using namespace mlpack;

double MaxIP::MaxNodeIP_(TreeType* reference_node) {

  // counting the split decisions 
  split_decisions_++;

  // compute maximum possible inner product 
  // between a point and a ball in terms of 
  // the ball's center and radius
  arma::vec q = queries_.col(query_);
  arma::vec centroid = reference_node->bound().center();

  // +1: Can be cached in the reference tree
  double c_norm = arma::norm(centroid, 2);

  assert(arma::norm(q, 2) == query_norms_(query_));

  double rad = std::sqrt(reference_node->bound().radius());

  double max_cos_qr = 1.0;

  if (mlpack::IO::HasParam("maxip/angle_prune")) { 
    // tighter bound of \max_{r \in B_p^R} <q,r> 
    //    = |q| \max_{r \in B_p^R} |r| cos <qr 
    //    \leq |q| \max_{r \in B_p^R} |r| \max_{r \in B_p^R} cos <qr 
    //    \leq |q| (|p|+R) if <qp \leq \max_r <pr
    //    \leq |q| (|p|+R) cos( <qp - \max_r <pr ) otherwise

    if (rad <= c_norm) {
      // +1
      double cos_qp = arma::dot(q, centroid) 
	/ (query_norms_(query_) * c_norm);
      double sin_qp = std::sqrt(1 - cos_qp * cos_qp);

      double max_sin_pr = rad / c_norm;
      double min_cos_pr = std::sqrt(1 - max_sin_pr * max_sin_pr);

      if (min_cos_pr > cos_qp) { // <qp \geq \max_r <pr
	// cos( <qp - <pr ) = cos <qp * cos <pr + sin <qp * sin <pr
	double cos_qp_max_pr = (cos_qp * min_cos_pr) 
	  + (sin_qp * max_sin_pr);

	// FIXIT: this should be made general to 
	// return negative values as well
	max_cos_qr = std::max(cos_qp_max_pr, 0.0);
      }
    }

  }

  // Otherwise :
  // simple bound of \max_{r \in B_p^R} <q,r> 
  //    = |q| \max_{r \in B_p^R} |r| cos <qr 
  //    \leq |q| \max_{r \in B_p^R} |r| \leq |q| (|p|+R)

  return (query_norms_(query_) * (c_norm + rad) * max_cos_qr);
}

double MaxIP::MaxNodeIP_(CTreeType* query_node,
			 TreeType* reference_node) {

  // counting the split decisions 
  split_decisions_++;

  // min_{q', q} cos <qq' = cos_w
  arma::vec q = query_node->bound().center();
  double cos_w = query_node->bound().radius();
  double sin_w = query_node->bound().radius_conjugate();

  // +1: Can cache it in the query tree
  double q_norm = arma::norm(q, 2);

  arma::vec centroid = reference_node->bound().center();

  // +1: can be cached in the reference tree
  double c_norm = arma::norm(centroid, 2);
  double rad = std::sqrt(reference_node->bound().radius());

  double max_cos_qp = 1.0;

  if (mlpack::IO::HasParam("maxip/angle_prune")) { 

    if (rad <= c_norm) {
      // cos <pq = cos_phi

      // +1
      double cos_phi = arma::dot(q, centroid) / (c_norm * q_norm);
      double sin_phi = std::sqrt(1 - cos_phi * cos_phi);

      // max_r sin <pr = sin_theta
      double sin_theta = rad / c_norm;
      double cos_theta = std::sqrt(1 - sin_theta * sin_theta);

      if ((cos_phi < cos_theta) && (cos_phi < cos_w)) { 
	// phi > theta and phi > w
	// computing cos(phi - theta)
	double cos_phi_theta 
	  = cos_phi * cos_theta + sin_phi * sin_theta;

	if (cos_phi_theta < cos_w) {
	  // phi - theta > w
	  // computing cos (phi - theta - w)
	  double cos_phi_theta_w = cos_phi_theta * cos_w;
	  cos_phi_theta_w
	    += (std::sqrt(1 - cos_phi_theta * cos_phi_theta)
		* sin_w);
	  max_cos_qp = std::max(cos_phi_theta_w, 0.0);
	}
      }
    }
  }

  return ((c_norm + rad) * max_cos_qp);
}


void MaxIP::ComputeBaseCase_(TreeType* reference_node) {
   
  assert(reference_node != NULL);
  assert(reference_node->is_leaf());
  assert(query_ >= 0);
  assert(query_ < queries_.n_cols);

    
  std::vector<std::pair<double, size_t> > candidates(knns_);

  // Get the query point from the matrix
  arma::vec q = queries_.col(query_); 

  size_t ind = query_*knns_;
  for(size_t i = 0; i < knns_; i++)
    candidates[i] = std::make_pair(max_ips_(ind+i),
				   max_ip_indices_(ind+i));
    
  // We'll do the same for the references
  for (size_t reference_index = reference_node->begin(); 
       reference_index < reference_node->end(); reference_index++) {

    arma::vec rpoint = references_.col(reference_index);

    // We'll use arma to find the inner product of the two vectors
    // +1
    double ip = arma::dot(q, rpoint);
    // If the reference point is greater than the current candidate, 
    // we'll update the candidate
    if (ip > max_ips_(ind+knns_-1)) {
      candidates.push_back(std::make_pair(ip, reference_index));
    }
  } // for reference_index

  std::sort(candidates.begin(), candidates.end());
  std::reverse(candidates.begin(), candidates.end());
  for(size_t i = 0; i < knns_; i++) {
    max_ips_(ind+i) = candidates[i].first;
    max_ip_indices_(ind+i) = candidates[i].second;
  }
  candidates.clear();

  // for now the query lower bounds are accessed from 
  // the variable 'max_ips_(query_ * knns_ + knns_ - 1)'

  distance_computations_ 
    += reference_node->end() - reference_node->begin();
         
} // ComputeBaseCase_


void MaxIP::ComputeNeighborsRecursion_(TreeType* reference_node, 
				       double upper_bound_ip) {

  assert(reference_node != NULL);
  //assert(upper_bound_ip == MaxNodeIP_(reference_node));

  if (upper_bound_ip < max_ips_((query_*knns_) + knns_ -1)) { 
    // Pruned by distance
    number_of_prunes_++;
  } else if (reference_node->is_leaf()) {
    // base case for the single tree case
    ComputeBaseCase_(reference_node);

  } else {
    // Recurse on both as above
    double left_ip = MaxNodeIP_(reference_node->left());
    double right_ip = MaxNodeIP_(reference_node->right());

    if (left_ip > right_ip) {
      ComputeNeighborsRecursion_(reference_node->left(), 
				 left_ip);
      ComputeNeighborsRecursion_(reference_node->right(),
				 right_ip);
    } else {
      ComputeNeighborsRecursion_(reference_node->right(),
				 right_ip);
      ComputeNeighborsRecursion_(reference_node->left(), 
				 left_ip);
    }
  }      
} // ComputeNeighborsRecursion_


void MaxIP::ComputeBaseCase_(CTreeType* query_node, 
			     TreeType* reference_node) {

  // Check that the pointers are not NULL
  assert(reference_node != NULL);
  assert(reference_node->is_leaf());
  assert(query_node != NULL);
  assert(query_node->is_leaf());

  // Used to find the query node's new lower bound
  double query_worst_p_cos_pq = DBL_MAX;
  bool new_bound = false;
    
  // Iterating over the queries individually
  for (query_ = query_node->begin();
       query_ < query_node->end(); query_++) {

    size_t ind = query_ * knns_;

    // checking if this node has potential
    double query_to_node_max_ip = MaxNodeIP_(reference_node);

    // assert(query_to_node_max_ip > 0.0);

    if (query_to_node_max_ip > max_ips_(ind + knns_ -1))
      // this node has potential
      ComputeBaseCase_(reference_node);

    double p_cos_pq = max_ips_(ind + knns_ -1)
      / query_norms_(query_);

    if (query_worst_p_cos_pq > p_cos_pq) {
      query_worst_p_cos_pq = p_cos_pq;
      new_bound = true;
    }
  } // for query_
  
  // Update the lower bound for the query_node
  if (new_bound) 
    query_node->stat().set_bound(query_worst_p_cos_pq);

} // ComputeBaseCase_
  

void MaxIP::CheckPrune(CTreeType* query_node, TreeType* ref_node) {

  size_t missed_nns = 0;
  double max_p_cos_pq = 0.0;
  double min_p_cos_pq = DBL_MAX;

  // Iterating over the queries individually
  for (query_ = query_node->begin();
       query_ < query_node->end(); query_++) {

    // Get the query point from the matrix
    arma::vec q = queries_.col(query_);
    size_t ind = query_ * knns_;

    double p_cos_qp = max_ips_(ind + knns_ -1) / query_norms_(query_);
    if (min_p_cos_pq > p_cos_qp)
      min_p_cos_pq = p_cos_qp;

    // We'll do the same for the references
    for (size_t reference_index = ref_node->begin(); 
	 reference_index < ref_node->end(); reference_index++) {

      arma::vec r = references_.col(reference_index);

      double ip = arma::dot(q, r);
      if (ip > max_ips_(ind+knns_-1))
	missed_nns++;

      double p_cos_pq = ip / query_norms_(query_);

      if (p_cos_pq > max_p_cos_pq)
	max_p_cos_pq = p_cos_pq;

    } // for reference_index
  } // for query_

  if (missed_nns > 0 || query_node->stat().bound() != min_p_cos_pq) 
    printf("Prune %zu - Missed candidates: %zu\n"
	   "QLBound: %lg, ActualQLBound: %lg\n"
	   "QRBound: %lg, ActualQRBound: %lg\n",
	   number_of_prunes_, missed_nns,
	   query_node->stat().bound(), min_p_cos_pq, 
	   MaxNodeIP_(query_node, ref_node), max_p_cos_pq);

}

void MaxIP::ComputeNeighborsRecursion_(CTreeType* query_node,
				       TreeType* reference_node, 
				       double upper_bound_p_cos_pq) {

  assert(query_node != NULL);
  assert(reference_node != NULL);
  //assert(upper_bound_p_cos_pq == MaxNodeIP_(query_node, reference_node));

  if (upper_bound_p_cos_pq < query_node->stat().bound()) { 
    // Pruned
    number_of_prunes_++;

    if (IO::HasParam("maxip/check_prune"))
      CheckPrune(query_node, reference_node);
  }
  // node->is_leaf() works as one would expect
  else if (query_node->is_leaf() && reference_node->is_leaf()) {
    // Base Case
    ComputeBaseCase_(query_node, reference_node);
  } else if (query_node->is_leaf()) {
    // Only query is a leaf
      
    // We'll order the computation by distance 
    double left_p_cos_pq = MaxNodeIP_(query_node,
				      reference_node->left());
    double right_p_cos_pq = MaxNodeIP_(query_node,
				       reference_node->right());
      
    if (left_p_cos_pq > right_p_cos_pq) {
      ComputeNeighborsRecursion_(query_node, reference_node->left(), 
				 left_p_cos_pq);
      ComputeNeighborsRecursion_(query_node, reference_node->right(), 
				 right_p_cos_pq);
    } else {
      ComputeNeighborsRecursion_(query_node, reference_node->right(), 
				 right_p_cos_pq);
      ComputeNeighborsRecursion_(query_node, reference_node->left(), 
				 left_p_cos_pq);
    }
  } else if (reference_node->is_leaf()) {
    // Only reference is a leaf 
    double left_p_cos_pq
      = MaxNodeIP_(query_node->left(), reference_node);
    double right_p_cos_pq
      = MaxNodeIP_(query_node->right(), reference_node);
      
    ComputeNeighborsRecursion_(query_node->left(), reference_node, 
			       left_p_cos_pq);
    ComputeNeighborsRecursion_(query_node->right(), reference_node, 
			       right_p_cos_pq);
      
    // We need to update the upper bound based on the new upper bounds of 
    // the children
    query_node->stat().set_bound(std::min(query_node->left()->stat().bound(),
					  query_node->right()->stat().bound()));
  } else {
    // Recurse on both as above
    double left_p_cos_pq = MaxNodeIP_(query_node->left(), 
				      reference_node->left());
    double right_p_cos_pq = MaxNodeIP_(query_node->left(), 
				       reference_node->right());
      
    if (left_p_cos_pq > right_p_cos_pq) {
      ComputeNeighborsRecursion_(query_node->left(),
				 reference_node->left(), 
				 left_p_cos_pq);
      ComputeNeighborsRecursion_(query_node->left(),
				 reference_node->right(), 
				 right_p_cos_pq);
    } else {
      ComputeNeighborsRecursion_(query_node->left(),
				 reference_node->right(), 
				 right_p_cos_pq);
      ComputeNeighborsRecursion_(query_node->left(),
				 reference_node->left(), 
				 left_p_cos_pq);
    }

    left_p_cos_pq = MaxNodeIP_(query_node->right(),
			       reference_node->left());
    right_p_cos_pq = MaxNodeIP_(query_node->right(), 
				reference_node->right());
      
    if (left_p_cos_pq > right_p_cos_pq) {
      ComputeNeighborsRecursion_(query_node->right(),
				 reference_node->left(), 
				 left_p_cos_pq);
      ComputeNeighborsRecursion_(query_node->right(),
				 reference_node->right(), 
				 right_p_cos_pq);
    } else {
      ComputeNeighborsRecursion_(query_node->right(),
				 reference_node->right(), 
				 right_p_cos_pq);
      ComputeNeighborsRecursion_(query_node->right(),
				 reference_node->left(), 
				 left_p_cos_pq);
    }
      
    // Update the upper bound as above
    query_node->stat().set_bound(std::min(query_node->left()->stat().bound(),
					  query_node->right()->stat().bound()));
  }
} // ComputeNeighborsRecursion_
  

void MaxIP::Init(const arma::mat& queries_in,
		 const arma::mat& references_in) {
    
    
  // track the number of prunes and computations
  number_of_prunes_ = 0;
  distance_computations_ = 0;
  split_decisions_ = 0;
    
  // Get the leaf size from the module
  leaf_size_ = mlpack::IO::GetParam<int>("maxip/leaf_size");
  // Make sure the leaf size is valid
  assert(leaf_size_ > 0);
    
  // Copy the matrices to the class members since they will be rearranged.  
  queries_ = queries_in;
  references_ = references_in;
    
  // The data sets need to have the same number of points
  assert(queries_.n_rows == references_.n_rows);
    
  // K-nearest neighbors initialization
  knns_ = mlpack::IO::GetParam<int>("maxip/knns");

  // Initialize the list of nearest neighbor candidates
  max_ip_indices_ 
    = -1 * arma::ones<arma::Col<size_t> >(queries_.n_cols * knns_, 1);
    
  // Initialize the vector of upper bounds for each point.
  // We do not consider negative values for inner products.
  max_ips_ = 0.0 * arma::ones<arma::vec>(queries_.n_cols * knns_, 1);

  // We'll time tree building
  mlpack::IO::StartTimer("tree_building");

  reference_tree_
    = proximity::MakeGenMetricTree<TreeType>(references_, 
					     leaf_size_,
					     &old_from_new_references_,
					     NULL);
    
  if (mlpack::IO::HasParam("maxip/dual_tree")) {
    query_tree_
      = proximity::MakeGenCosineTree<CTreeType>(queries_,
						leaf_size_,
						&old_from_new_queries_,
						NULL);
  }

  // saving the query norms beforehand to use 
  // in the tree-based searches -- need to do it 
  // after the shuffle to correspond to correct indices
  query_norms_ = 0.0 * arma::ones<arma::vec>(queries_.n_cols);
  for (size_t i = 0; i < queries_.n_cols; i++)
    query_norms_(i) = arma::norm(queries_.col(i), 2);
      
  // Stop the timer we started above
  mlpack::IO::StopTimer("tree_building");

} // Init


void MaxIP::InitNaive(const arma::mat& queries_in, 
		      const arma::mat& references_in) {
    
  queries_ = queries_in;
  references_ = references_in;
    
  // track the number of prunes and computations
  number_of_prunes_ = 0;
  distance_computations_ = 0;
  split_decisions_ = 0;
    
  // The data sets need to have the same number of dimensions
  assert(queries_.n_rows == references_.n_rows);
    
  // K-nearest neighbors initialization
  knns_ = mlpack::IO::GetParam<int>("maxip/knns");
  
  // Initialize the list of nearest neighbor candidates
  max_ip_indices_
    = -1 * arma::ones<arma::Col<size_t> >(queries_.n_cols * knns_, 1);
    
  // Initialize the vector of upper bounds for each point.
  // We do not consider negative values for inner products.
  max_ips_ = 0.0 * arma::ones<arma::vec>(queries_.n_cols * knns_, 1);

  // The only difference is that we set leaf_size_ to be large enough 
  // that each tree has only one node
  leaf_size_ = std::max(queries_.n_cols, references_.n_cols) + 1;

  // We'll time tree building
  mlpack::IO::StartTimer("tree_building");
    
  reference_tree_
    = proximity::MakeGenMetricTree<TreeType>(references_, 
					     leaf_size_,
					     &old_from_new_references_,
					     NULL);

  // Stop the timer we started above
  mlpack::IO::StopTimer("tree_building");
    
} // InitNaive
  
void MaxIP::WarmInit(size_t knns) {
    
    
  // track the number of prunes and computations
  number_of_prunes_ = 0;
  distance_computations_ = 0;
  split_decisions_ = 0;
    
  // K-nearest neighbors initialization
  knns_ = knns;

  // Initialize the list of nearest neighbor candidates
  max_ip_indices_ 
    = -1 * arma::ones<arma::Col<size_t> >(queries_.n_cols * knns_, 1);
    
  // Initialize the vector of upper bounds for each point.
  // We do not consider negative values for inner products.
  max_ips_ = 0.0 * arma::ones<arma::vec>(queries_.n_cols * knns_, 1);

  // need to reset the querystats in the Query Tree
  if (mlpack::IO::HasParam("maxip/dual_tree"))
    if (query_tree_ != NULL)
      reset_tree_(query_tree_);

} // WarmInit

void MaxIP::reset_tree_(CTreeType* tree) {
  assert(tree != NULL);
  tree->stat().set_bound(0.0);

  if (!tree->is_leaf()) {
    reset_tree_(tree->left());
    reset_tree_(tree->right());
  }
}

double MaxIP::ComputeNeighbors(arma::Col<size_t>* resulting_neighbors,
			       arma::vec* ips) {


  resulting_neighbors->set_size(max_ips_.n_elem);
  ips->set_size(max_ips_.n_elem);

  if (mlpack::IO::HasParam("maxip/dual_tree")) {
    // do dual-tree search
    mlpack::IO::Info << "DUAL-TREE Search: " << std::endl;

    ComputeNeighborsRecursion_(query_tree_, reference_tree_,
			       MaxNodeIP_(query_tree_, reference_tree_));

    for (size_t i = 0; i < max_ips_.n_elem; i++) {
      size_t query = old_from_new_queries_(i / knns_);
      assert(max_ip_indices_(i) != (size_t) -1 || max_ips_(i) == 0.0);
      (*resulting_neighbors)(query*knns_+ i%knns_)
	= old_from_new_references_(max_ip_indices_(i));
      (*ips)(query*knns_+ i%knns_) = max_ips_(i);
    }


  } else {
    // do single-tree search
    mlpack::IO::Info << "SINGLE-TREE Search: " << std::endl;

    for (query_ = 0; query_ < queries_.n_cols; ++query_) {
      ComputeNeighborsRecursion_(reference_tree_, 
				 MaxNodeIP_(reference_tree_));
    }


    for (size_t i = 0; i < max_ips_.n_elem; i++) {
      size_t query = i/knns_;
      (*resulting_neighbors)(query*knns_+ i%knns_)
	= old_from_new_references_(max_ip_indices_(i));
      (*ips)(query*knns_+ i%knns_) = max_ips_(i);
    }
  }

  mlpack::IO::Info << "Tree-based Search - Number of prunes: " 
		   << number_of_prunes_ << std::endl;
  mlpack::IO::Info << "\t \t Avg. # of DC: " 
		   << (double) distance_computations_ 
    / (double) queries_.n_cols << std::endl;
  mlpack::IO::Info << "\t \t Avg. # of SD: " 
		   << (double) split_decisions_ 
    / (double) queries_.n_cols << std::endl;

  return (double) (distance_computations_ + split_decisions_)
    / (double) queries_.n_cols;
} // ComputeNeighbors
  
double MaxIP::ComputeNaive(arma::Col<size_t>* resulting_neighbors,
			   arma::vec* ips) {

  for (query_ = 0; query_ < queries_.n_cols; ++query_) {
    ComputeBaseCase_(reference_tree_);
  }

  resulting_neighbors->set_size(max_ips_.n_elem);
  ips->set_size(max_ips_.n_elem);

  for (size_t i = 0; i < max_ips_.n_elem; i++) {
    size_t query = i/knns_;
    (*resulting_neighbors)(query*knns_+ i%knns_)
      = old_from_new_references_(max_ip_indices_(i));
    (*ips)(query*knns_+ i%knns_) = max_ips_(i);
  }
    
  mlpack::IO::Info << "Brute-force Search - Number of prunes: " 
		   << number_of_prunes_ << std::endl;
  mlpack::IO::Info << "\t \t Avg. # of DC: " 
		   << (double) distance_computations_ 
    / (double) queries_.n_cols << std::endl;
  mlpack::IO::Info << "\t \t Avg. # of SD: " 
		   << (double) split_decisions_ 
    / (double) queries_.n_cols << std::endl;

  return (double) (distance_computations_ + split_decisions_)
    / (double) queries_.n_cols;
}