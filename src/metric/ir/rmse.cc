/*
 * QuickRank - A C++ suite of Learning to Rank algorithms
 * Webpage: http://quickrank.isti.cnr.it/
 * Contact: quickrank@isti.cnr.it
 *
 * Unless explicitly acquired and licensed from Licensor under another
 * license, the contents of this file are subject to the Reciprocal Public
 * License ("RPL") Version 1.5, or subsequent versions as allowed by the RPL,
 * and You may not copy or use this file in either source code or executable
 * form, except in compliance with the terms and conditions of the RPL.
 *
 * All software distributed under the RPL is provided strictly on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESS OR IMPLIED, AND
 * LICENSOR HEREBY DISCLAIMS ALL SUCH WARRANTIES, INCLUDING WITHOUT
 * LIMITATION, ANY WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE, QUIET ENJOYMENT, OR NON-INFRINGEMENT. See the RPL for specific
 * language governing rights and limitations under the RPL.
 *
 * Contributor:
 *   HPC. Laboratory - ISTI - CNR - http://hpc.isti.cnr.it/
 */
#include <cmath>
#include <algorithm>

#include "metric/ir/rmse.h"

namespace quickrank {
namespace metric {
namespace ir {

const std::string Rmse::NAME_ = "RMSE";

MetricScore Rmse::evaluate_result_list(const quickrank::data::QueryResults *rl,
                                       const Score *scores) const {
  size_t size = std::min(cutoff(), rl->num_results());
  if (size == 0)
    return 0.0;

  MetricScore sse = 0.0f;
  for (size_t i = 0; i < size; ++i)
    sse += pow(scores[i] - rl->labels()[i], 2);
  return sse;
}

MetricScore Rmse::evaluate_dataset(
    const std::shared_ptr<data::Dataset> dataset, const Score *scores) const {

  size_t size = std::min(cutoff(), (size_t) dataset->num_queries());
  if (size == 0)
    return 0.0;

  MetricScore sse = 0.0f;
  for (size_t q = 0; q < dataset->num_queries(); q++) {
    std::shared_ptr<data::QueryResults> r = dataset->getQueryResults(q);
    sse += evaluate_result_list(r.get(), scores);
    scores += r->num_results();
  }

  return -sqrt(sse / dataset->num_instances());
}

MetricScore Rmse::evaluate_dataset(
    const std::shared_ptr<data::VerticalDataset> dataset,
    const Score *scores) const {

  size_t size = std::min(cutoff(), (size_t) dataset->num_queries());
  if (size == 0)
    return 0.0;

  MetricScore sse = 0.0f;
  for (size_t q = 0; q < dataset->num_queries(); q++) {
    std::shared_ptr<data::QueryResults> r = dataset->getQueryResults(q);
    sse += evaluate_result_list(r.get(), scores);
    scores += r->num_results();
  }

  return -sqrt(sse / dataset->num_instances());
}

std::unique_ptr<Jacobian> Rmse::jacobian(
    std::shared_ptr<data::RankedResults> ranked) const {

  // count = (ql.qid<nrelevantdocs && relevantdocs[ql.qid]>count) ? relevantdocs[ql.qid] : count;
  std::unique_ptr<Jacobian> changes = std::unique_ptr<Jacobian>(
      new Jacobian(ranked->num_results()));
  // RMSE is not affected by the rank...
  return changes;
}

std::ostream &Rmse::put(std::ostream &os) const {
  if (cutoff() != Metric::NO_CUTOFF)
    return os << name() << "@" << cutoff();
  else
    return os << name();
}

}  // namespace ir
}  // namespace metric
}  // namespace quickrank
