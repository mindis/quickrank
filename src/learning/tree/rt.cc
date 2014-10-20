#include "learning/tree/rt.h"

void DevianceMaxHeap::push_chidrenof(RTNode *parent) {
  push(parent->left->deviance, parent->left);
  push(parent->right->deviance, parent->right);
}
void DevianceMaxHeap::pop() {
  RTNode *node = top();
  delete [] node->sampleids,
  delete node->hist;
  node->sampleids = NULL,
      node->nsampleids = 0,
      node->hist = NULL;
  rt_maxheap::pop();
}

RegressionTree::~RegressionTree() {
  if(root) {
    delete [] root->sampleids;
    root->sampleids = NULL,
        root->nsampleids = 0;
  }
  //if leaves[0] is the root, hist cannot be deallocated and sampleids has been already deallocated
  for(unsigned int i=0; i<nleaves; ++i)
    if(leaves[i]!=root) {
      delete [] leaves[i]->sampleids,
      delete leaves[i]->hist;
      leaves[i]->hist = NULL,
          leaves[i]->sampleids = NULL,
          leaves[i]->nsampleids = 0;
    }
  free(leaves);
}

void RegressionTree::fit(RTNodeHistogram *hist) {
  DevianceMaxHeap heap(nrequiredleaves);
  unsigned int taken = 0;
  unsigned int nsampleids = training_dataset->num_instances(); //set->get_ndatapoints();
  unsigned int *sampleids = new unsigned int[nsampleids];
#pragma omp parallel for
  for(unsigned int i=0; i<nsampleids; ++i)
    sampleids[i] = i;
  root = new RTNode(sampleids, nsampleids, DBL_MAX, 0.0, hist);
  if(split(root, 1.0f, false))
    heap.push_chidrenof(root);
  while(heap.is_notempty() && (nrequiredleaves==0 or taken+heap.get_size()<nrequiredleaves)) {
    //get node with highest deviance from heap
    RTNode *node = heap.top();
    // TODO: Cla missing check non leaf size or avoid putting them into the heap
    //try split current node
    if(split(node, 1.0f, false)) heap.push_chidrenof(node); else ++taken; //unsplitable (i.e. null variance, or after split variance is higher than before, or #samples<minlsd)
    //remove node from heap
    heap.pop();
  }
  //visit tree and save leaves in a leaves[] array
  unsigned int capacity = nrequiredleaves;
  leaves = capacity ? (RTNode**)malloc(sizeof(RTNode*)*capacity) : NULL,
      nleaves = 0;
  root->save_leaves(leaves, nleaves, capacity);

  // TODO: (by cla) is memory of "unpopped" de-allocated?
}

double RegressionTree::update_output(double const *pseudoresponses, double const *cachedweights) {
  double maxlabel = -DBL_MAX;
#pragma omp parallel for reduction(max:maxlabel)
  for(unsigned int i=0; i<nleaves; ++i) {
    double s1 = 0.0;
    double s2 = 0.0;
    const unsigned int nsampleids = leaves[i]->nsampleids;
    const unsigned int *sampleids = leaves[i]->sampleids;
    for(unsigned int j=0; j<nsampleids; ++j) {
      unsigned int k = sampleids[j];
      s1 += pseudoresponses[k];
      s2 += cachedweights[k];
      //					printf("## %d: %.15f \t %.15f \n", k, pseudoresponses[k], cachedweights[k]);
    }
    leaves[i]->avglabel = s2>=DBL_EPSILON ? s1/s2 : 0.0;

    //				printf("## Leaf with size: %d  ##  s1/s2: %.15f / %.15f = %.15f\n", nsampleids, s1, s2, leaves[i]->avglabel);

    if(leaves[i]->avglabel>maxlabel)
      maxlabel = leaves[i]->avglabel;
  }
  return maxlabel;
}

bool RegressionTree::split(RTNode *node, const float featuresamplingrate, const bool require_devianceltparent) {
  //			printf("### Splitting a node of size: %d and deviance: %f\n", node->nsampleids, node->deviance);

  if(node->deviance>0.0f) {
    // const double initvar = require_devianceltparent ? node->deviance : -1; //DBL_MAX;
    const double initvar = -1; // minimum split score
    //get current nod hidtogram pointer
    RTNodeHistogram *h = node->hist;
    //featureidxs to be used for tree splitnodeting
    unsigned int nfeaturesamples = training_dataset->num_features(); //training_set->get_nfeatures();
    unsigned int *featuresamples = NULL;
    //need to make a sub-sampling
    if(featuresamplingrate<1.0f) {
      featuresamples = new unsigned int[nfeaturesamples];
      for(unsigned int i=0; i<nfeaturesamples; ++i)
        featuresamples[i] = i;
      //need to make a sub-sampling
      const unsigned int reduced_nfeaturesamples = floor(featuresamplingrate*nfeaturesamples);
      while(nfeaturesamples>reduced_nfeaturesamples && nfeaturesamples>1) {
        const unsigned int i = rand()%nfeaturesamples;
        featuresamples[i] = featuresamples[--nfeaturesamples];
      }
    }
    //find best split
    const int nth = omp_get_num_procs();
    double* thread_best_score = new double [nth]; // double thread_minvar[nth];
    unsigned int* thread_best_featureidx = new unsigned int [nth]; // unsigned int thread_best_featureidx[nth];
    unsigned int* thread_best_thresholdid = new unsigned int [nth]; // unsigned int thread_best_thresholdid[nth];
    for(int i=0; i<nth; ++i)
      thread_best_score[i] = initvar,
      thread_best_featureidx[i] = uint_max,
      thread_best_thresholdid[i] = uint_max;
#pragma omp parallel for
    for(unsigned int i=0; i<nfeaturesamples; ++i) {
      //get feature idx
      const unsigned int f = featuresamples ? featuresamples[i] : i;
      //get thread identification number
      const int ith = omp_get_thread_num();
      //define pointer shortcuts
      double *sumlabels = h->sumlbl[f];
      unsigned int *samplecount = h->count[f];
      //get last elements
      unsigned int threshold_size = h->thresholds_size[f];
      double s = sumlabels[threshold_size-1];
      unsigned int c = samplecount[threshold_size-1];

      //					if (f==25)
      //						printf("### threshold size: %d\n", threshold_size);


      //looking for the feature that minimizes sumvar
      for(unsigned int t=0; t<threshold_size; ++t) {
        unsigned int lcount = samplecount[t];
        unsigned int rcount = c-lcount;
        if(lcount>=minls && rcount>=minls)  {
          double lsum = sumlabels[t];
          double rsum = s-lsum;
          double score = lsum*lsum/(double)lcount + rsum*rsum/(double)rcount;

          //							if (c==4992 && ( training_set->get_featureid(f)==51 || training_set->get_featureid(f)==128) )
          //								printf("### fx:%d \t t:%d  \t lc:%d \t rc:%d \t sum:%f \t S:%f\n",
          //										training_set->get_featureid(f), t, lcount, rcount, sumlabels[t], score);
          if(score>thread_best_score[ith])
            thread_best_score[ith] = score,
            thread_best_featureidx[ith] = f,
            thread_best_thresholdid[ith] = t;
        } // else { if (f==25) printf("### stop because of too few elements\n"); }
      }
    }
    //free feature samples
    delete [] featuresamples;
    //get best minvar among thread partial results
    double best_score = thread_best_score[0];
    unsigned int best_featureidx = thread_best_featureidx[0];
    unsigned int best_thresholdid = thread_best_thresholdid[0];
    for(int i=1; i<nth; ++i)
      if(thread_best_score[i]>best_score)
        best_score = thread_best_score[i],
        best_featureidx = thread_best_featureidx[i],
        best_thresholdid = thread_best_thresholdid[i];
    // free some memory
    delete [] thread_best_score;
    delete [] featuresamples;
    delete [] thread_best_featureidx;
    delete [] thread_best_thresholdid;
    //if minvar is the same of initvalue then the node is unsplitable
    if(best_score==initvar)
      return false;

    //set some result values related to minvar
    const unsigned int 	last_thresholdidx 	= h->thresholds_size[best_featureidx]-1;
    const float        	best_threshold 		= h->thresholds[best_featureidx][best_thresholdid];

    const unsigned int	count 	= h->count[best_featureidx][last_thresholdidx];
    const double 		sum   	= h->sumlbl[best_featureidx][last_thresholdidx];
    const double 		sqsum 	= h->sqsumlbl[best_featureidx][last_thresholdidx];

    const unsigned int	lcount	= h->count[best_featureidx][best_thresholdid];
    const double 		lsum 	= h->sumlbl[best_featureidx][best_thresholdid];
    const double 		lsqsum	= h->sqsumlbl[best_featureidx][best_thresholdid];

    const unsigned int	rcount	= count-lcount;
    const double 		rsum 	= sum-lsum;
    const double 		rsqsum	= sqsum-lsqsum;

    //split samples between left and right child
    unsigned int *lsamples = new unsigned int[lcount], lsize = 0;
    unsigned int *rsamples = new unsigned int[rcount], rsize = 0;
    float const* features = training_dataset->at(0,best_featureidx); //training_set->get_fvector(best_featureidx);
    for(unsigned int i=0, nsampleids=node->nsampleids; i<nsampleids; ++i) {
      unsigned int k = node->sampleids[i];
      if(features[k]<=best_threshold) lsamples[lsize++] = k; else rsamples[rsize++] = k;
      //					if (k<=20)
      //						if(features[k]<=best_threshold) printf("### %d goes left\n", k);
      //						else printf("### %d goes right\n", k);
    }
    //create histograms for children
    RTNodeHistogram *lhist = new RTNodeHistogram(node->hist, lsamples, lsize, training_labels);
    RTNodeHistogram *rhist = NULL;
    if(node==root)
      rhist = new RTNodeHistogram(node->hist, lhist);
    else {
      //save some new/delete by converting parent histogram into the right-child one
      node->hist->transform_intorightchild(lhist),
          rhist = node->hist;
      node->hist = NULL;
    }

    // deviances or variances
    double deviance  = sqsum - sum*sum/(double)count;
    double ldeviance = lsqsum - lsum*lsum/(double)lcount;
    double rdeviance = rsqsum - rsum*rsum/(double)rcount;

    //update current node
    node->set_feature(best_featureidx, best_featureidx+1/*training_set->get_featureid(best_featureidx)*/),
        node->threshold = best_threshold,
        node->deviance = deviance,
        //create children
        node->left = new RTNode(lsamples, lsize, ldeviance, lsum, lhist),
        node->right = new RTNode(rsamples, rsize, rdeviance, rsum, rhist);

    //				printf("### Best Feature fx:%d \t t:%d \t s:%f\n", node->get_feature_id(), best_thresholdid, best_score);
    //
    //				printf("### Current node sum: %.15f sqsum: %.15f\n", sum, sqsum);
    //				printf("### Right subtree size:%d\tdev:%.6f\tsum:%.6f\tsqsum:%.6f\n", rsize, rdeviance, rsum, rsqsum);
    //				printf("### Left  subtree size:%d\tdev:%.6f\tsum:%.6f\tsqsum_%.6f\n", lsize, ldeviance, lsum, lsqsum);

    // rhist->quick_dump(128,10);
    // lhist->quick_dump(25,10);


    return true;
  }
  return false;
}
