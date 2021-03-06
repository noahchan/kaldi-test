// online2/online-nnet3-decoding.h

// Copyright 2014  Johns Hopkins University (author: Daniel Povey)
//           2016  Api.ai (Author: Ilya Platonov)

// See ../../COPYING for clarification regarding multiple authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// THIS CODE IS PROVIDED *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
// WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
// MERCHANTABLITY OR NON-INFRINGEMENT.
// See the Apache 2 License for the specific language governing permissions and
// limitations under the License.


#ifndef KALDI_ONLINE2_ONLINE_NNET3_DECODING_H_
#define KALDI_ONLINE2_ONLINE_NNET3_DECODING_H_

#include <string>
#include <vector>
#include <deque>

#include "decodable-online-looped.h"
#include "matrix-lib.h"
#include "common-utils.h"
#include "kaldi-error.h"
#include "online-feature-itf.h"
#include "online-endpoint.h"
#include "online-nnet2-feature-pipeline.h"
#include "lattice-faster-online-decoder.h"
#include "transition-model.h"
//#include "posterior.h"

namespace kaldi {
/// @addtogroup  onlinedecoding OnlineDecoding
/// @{


/**
   You will instantiate this class when you want to decode a single
   utterance using the online-decoding setup for neural nets.
*/
class SingleUtteranceNnet3Decoder {
 public:

  // Constructor. The pointer 'features' is not being given to this class to own
  // and deallocate, it is owned externally.
  SingleUtteranceNnet3Decoder(const LatticeFasterDecoderConfig &decoder_opts,
                              const TransitionModel &trans_model,
                              const nnet3::DecodableNnetSimpleLoopedInfo &info,
                              Wfst *wfst,
                              OnlineNnet2FeaturePipeline *features);

  /// advance the decoding as far as we can.
  void AdvanceDecoding();

  /// Finalizes the decoding. Cleans up and prunes remaining tokens, so the
  /// GetLattice() call will return faster.  You must not call this before
  /// calling (TerminateDecoding() or InputIsFinished()) and then Wait().
  void FinalizeDecoding();

  int32 NumFramesDecoded() const;

  /// Gets the lattice.  The output lattice has any acoustic scaling in it
  /// (which will typically be desirable in an online-decoding context); if you
  /// want an un-scaled lattice, scale it using ScaleLattice() with the inverse
  /// of the acoustic weight.  "end_of_utterance" will be true if you want the
  /// final-probs to be included.
  //void GetLattice(bool end_of_utterance,
  //                CompactLattice *clat) const;

  /// Outputs an FST corresponding to the single best path through the current
  /// lattice. If "use_final_probs" is true AND we reached the final-state of
  /// the graph then it will include those as final-probs, else it will treat
  /// all final-probs as one.
  void GetBestPath(bool end_of_utterance,
                   std::vector<int> *olabel) const;


  /// This function calls EndpointDetected from online-endpoint.h,
  /// with the required arguments.
  bool EndpointDetected(const OnlineEndpointConfig &config,float *sil_length_acc);

  const LatticeFasterOnlineDecoder &Decoder() const { return decoder_; }

  ~SingleUtteranceNnet3Decoder() { }
 private:

  const LatticeFasterDecoderConfig &decoder_opts_;

  // this is remembered from the constructor; it's ultimately
  // derived from calling FrameShiftInSeconds() on the feature pipeline.
  BaseFloat input_feature_frame_shift_in_seconds_;

  // we need to keep a reference to the transition model around only because
  // it's needed by the endpointing code.
  const TransitionModel &trans_model_;

  nnet3::DecodableAmNnetLoopedOnline decodable_;

  LatticeFasterOnlineDecoder decoder_;

};


/// @} End of "addtogroup onlinedecoding"

}  // namespace kaldi



#endif  // KALDI_ONLINE2_ONLINE_NNET3_DECODING_H_
