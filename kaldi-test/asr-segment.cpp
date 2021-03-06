
#include "stdafx.h"

#include "asr-segment.h"
#include "online-nnet3-decoding.h"
#include "nnet-utils.h"
#include "asr-online-api.h"
#include <numeric>
namespace kaldi {


	int asrSegment(bool *more_data, AsrShareOpt *asrShareOpt, AsrShareResource *asrShareResource, WaveDataInfo_old *waveDataInfo) {

		OnlineNnet2FeaturePipeline feature_pipeline(*(asrShareOpt->feature_info));

		SingleUtteranceNnet3Decoder decoder(*asrShareOpt->decoder_opts, *asrShareResource->trans_model,
			*asrShareOpt->decodable_info,
			asrShareResource->wfst, &feature_pipeline);
		Vector<BaseFloat> wave_part = Vector<BaseFloat>(waveDataInfo->chunk_length);
		BaseFloat last_traceback = 0.0;
		BaseFloat num_seconds_decoded = 0.0;

		while (true) {

			*more_data = wave_part.ReadFromQueue(&(waveDataInfo->waveQueue));

			feature_pipeline.AcceptWaveform(waveDataInfo->sample_rate, wave_part);
			if (!*more_data) {
				feature_pipeline.InputFinished();
			}

			decoder.AdvanceDecoding();
			num_seconds_decoded += 1.0 * wave_part.Dim() / waveDataInfo->sample_rate;
			//waveDataInfo->total_time_decoded += 1.0 * wave_part.Dim() / waveDataInfo->sample_rate;

			if (!*more_data) {
				break;
			}
			//endpoint
			bool do_endpoint = true;
			if (do_endpoint && (decoder.NumFramesDecoded() > 0) &&decoder.EndpointDetected(*asrShareOpt->endpoint_opts,NULL))
				break;
			//partial result
			if ((num_seconds_decoded - last_traceback > waveDataInfo->traceback_period_secs)
				&& (decoder.NumFramesDecoded() > 0)) {
				bool end_of_utterance = true;
				std::vector<int> olabel;
				std::vector<stdstring> resultText;
				decoder.GetBestPath(end_of_utterance, &olabel);
				outputText(asrShareResource->wordSymbol, olabel, &resultText);
			}
		}

		//final result
		if (num_seconds_decoded > 0.1) {
			decoder.FinalizeDecoding();
			bool end_of_utterance = true;
			std::vector<int> olabel;
			std::vector<stdstring> resultText;
			decoder.GetBestPath(end_of_utterance, &olabel);
			outputText(asrShareResource->wordSymbol, olabel,&resultText);
			std::cout << std::endl;
			//长段静音
			if (resultText.size() == 0)
				*more_data = false;
		}
		return 0;
	}


	int asrSegmentSplice(AsrShareOpt *asrShareOpt, AsrShareResource *asrShareResource, const WaveSpliceData waveSpliceData, DecoderSaveState *decoderState, WaveDataInfo *waveDataInfo) {

		OnlineNnet2FeaturePipeline *&feature_pipeline = decoderState->feature_pipeline;

		SingleUtteranceNnet3Decoder *&decoder = decoderState->decoder;

		Vector<BaseFloat> wave_part = Vector<BaseFloat>(waveSpliceData.length);
		BaseFloat &last_traceback = decoderState->last_trackback;
		BaseFloat &num_seconds_decoded = decoderState->num_seconds_decoded;
		float &last_sentence_end = decoderState->last_sentence_end;
		float &sil_length_acc = decoderState->sil_length_acc;

		int &num_done = decoderState->num_done;
		int num_process = waveSpliceData.num_record;

		bool eos = waveSpliceData.eos;
		wave_part.ReadFromSpliceData(waveSpliceData.data, waveSpliceData.length);

		//*more_data = wave_part.ReadFromQueue(&(waveDataInfo->waveQueue));

		feature_pipeline->AcceptWaveform(waveDataInfo->sample_rate, wave_part);
		if (num_process== waveDataInfo->num_pushed && eos) {
			feature_pipeline->InputFinished();
			eos = true;
		}

		decoder->AdvanceDecoding();
		num_seconds_decoded += 1.0 * wave_part.Dim() / waveDataInfo->sample_rate;
		//waveDataInfo->total_time_decoded += 1.0 * wave_part.Dim() / waveDataInfo->sample_rate;

		bool vad_silence = false;

		if (!eos) {
			//endpoint
			bool do_endpoint = true;
			if (do_endpoint && (decoder->NumFramesDecoded() > 0) && decoder->EndpointDetected((*asrShareOpt->endpoint_opts),&sil_length_acc)) {
				vad_silence = true;
			}
		}

		if(!eos && !vad_silence) {//not final 
			//partial result
			if ((num_seconds_decoded - last_traceback > waveDataInfo->traceback_period_secs)
				&& (decoder->NumFramesDecoded() > 0)) {
				bool end_of_utterance = true;
				std::vector<int> olabel;
				std::vector<stdstring> resultText;
				decoder->GetBestPath(end_of_utterance, &olabel);
				outputText(asrShareResource->wordSymbol, olabel, &resultText);
				void(*fp_partial)(void * userId, stdchar* result_text);
				fp_partial = asr_online_partial_callback;
				stdstring sum;
				sum= std::accumulate(resultText.begin(), resultText.end(), sum);
				fp_partial((char*)waveDataInfo->userId, (stdchar*)sum.c_str());
				last_traceback = num_seconds_decoded;
				}
		}else {//final result
			if (num_seconds_decoded > 0.1) {
				decoder->FinalizeDecoding();
				bool end_of_utterance = true;
				std::vector<int> olabel;
				std::vector<stdstring> resultText;
				decoder->GetBestPath(end_of_utterance, &olabel);
				outputText(asrShareResource->wordSymbol, olabel, &resultText);
				void(*fp_final)(void * userId,stdchar* result_text, float start_time, float end_time);
				fp_final = asr_online_final_callback;
				stdstring sum;
				sum= std::accumulate(resultText.begin(), resultText.end(), sum);
				(decoderState->waveFinalResult).push_back(sum);

				//sum = sum + L",";
				fp_final((char*)waveDataInfo->userId, (stdchar*)sum.c_str(), last_sentence_end, num_seconds_decoded);

				//最终的识别结果
				if (eos) {
					void(*fp_wave_final)(void * userId, stdchar* result_text);
					fp_wave_final = asr_online_wave_final_callback;
					stdstring sum_last;
					sum_last = std::accumulate(decoderState->waveFinalResult.begin(), decoderState->waveFinalResult.end(), sum_last);
					fp_wave_final((char*)waveDataInfo->userId, (stdchar*)sum_last.c_str());
				}

				last_sentence_end = num_seconds_decoded;
				//std::cout << std::endl;
				//长段静音
				if (resultText.size() == 0 || eos == true) {//一句话为空，结束识别
					if (feature_pipeline != NULL) {
						delete feature_pipeline;
						feature_pipeline = NULL;
					}
					if (decoder != NULL) {
						delete decoder;
						decoder = NULL;
					}
					waveDataInfo->flag_end = true;
				}else { // 一句话结束，重新开始识别新的一句话
					if (feature_pipeline != NULL) {
						delete feature_pipeline;
						feature_pipeline = new OnlineNnet2FeaturePipeline(*(asrShareOpt->feature_info));
					}
					if (decoder != NULL) {
						delete decoder;
						decoder = new SingleUtteranceNnet3Decoder(*(asrShareOpt->decoder_opts), *asrShareResource->trans_model,
							*(asrShareOpt->decodable_info),
							asrShareResource->wfst, feature_pipeline);
					}			
					//重复一小段数据，防止丢字
					bool needsubvector = (waveSpliceData.length <= 3200);
					if (needsubvector) {
						feature_pipeline->AcceptWaveform(waveDataInfo->sample_rate, wave_part);
						decoder->AdvanceDecoding();
						//num_seconds_decoded += 1.0 * wave_part.Dim() / waveDataInfo->sample_rate;
					}
					else {
						SubVector<BaseFloat> subwave(wave_part, waveSpliceData.length-3200,3200);

						feature_pipeline->AcceptWaveform(waveDataInfo->sample_rate, wave_part);
						decoder->AdvanceDecoding();
						//num_seconds_decoded += 1.0 * wave_part.Dim() / waveDataInfo->sample_rate;
					}

				}// 一句话结束，重新开始识别新的一句话
					
			}
		}
		num_done++;
		return 0;
	}

	int asrOnlineLoop(AsrShareOpt *asrShareOpt, AsrShareResource *asrShareResource, WaveDataInfo_old *waveDataInfo) {
		Timer timer;
		bool more_data = true;
		while (more_data) {
			asrSegment(&more_data, asrShareOpt, asrShareResource, waveDataInfo);
		}
		std::cout <<"used time ="<< timer.Elapsed() << std::endl;

		return 0;
	}

	int asrLoadResource(const char* wordsName, const char*modelName, const char* wfstName, AsrShareResource *asrShareResource) {

		//读入词表
		std::vector<stdstring> *wordSymbol =new std::vector<stdstring>();
		asrShareResource->wordSymbol = wordSymbol;
		readSymbol(wordsName, asrShareResource->wordSymbol  );



		//加载声学模型
	//	std::string nnet3_rxfilename = "final.mdl";
		TransitionModel *trans_model=new TransitionModel();
		nnet3::AmNnetSimple *am_nnet=new nnet3::AmNnetSimple();
		{
			bool binary;
			Input ki(modelName, &binary);
			trans_model->Read(ki.Stream(), binary);

			am_nnet->Read(ki.Stream(), binary);
			SetBatchnormTestMode(true, &(am_nnet->GetNnet()));
			SetDropoutTestMode(true, &(am_nnet->GetNnet()));
			//nnet3::CollapseModel(nnet3::CollapseModelConfig(), &(am_nnet.GetNnet()));
		}

		 
		std::filebuf file;
		file.open(wfstName, std::ios::in | std::ios::binary);
		std::istream is(&file);

		Wfst *wfst = new Wfst();
		wfst->ReadHead(is);
		wfst->wfstRead(is);

		//asrShareResource->wordSymbol = &wordSymbol;
		(asrShareResource)->am_nnet = am_nnet;
		(asrShareResource)->trans_model = trans_model;
		(asrShareResource)->wfst = wfst;

		return 0;
	}

	int asrSetWaveInfo(WaveDataInfo_old *waveDataInfo) {
		waveDataInfo->chunk_length = 6000;
		waveDataInfo->sample_rate = 8000;  //8k-model
		waveDataInfo->traceback_period_secs = 0.25;
		return 0;
	}

	int asrSetShareOpt(AsrShareOpt *asrShareOpt, AsrShareResource* asrShareResource){

		OnlineNnet2FeaturePipelineConfig *feature_opts=new OnlineNnet2FeaturePipelineConfig();
		nnet3::NnetSimpleLoopedComputationOptions *decodable_opts =new nnet3::NnetSimpleLoopedComputationOptions();
		LatticeFasterDecoderConfig *decoder_opts =new LatticeFasterDecoderConfig();
		OnlineEndpointConfig *endpoint_opts =new OnlineEndpointConfig();

		BaseFloat chunk_length_secs = 0.18;
		//bool do_endpointing = false;
		bool online = true;

		OnlineNnet2FeaturePipelineInfo *feature_info=new OnlineNnet2FeaturePipelineInfo(*feature_opts);
		nnet3::DecodableNnetSimpleLoopedInfo *decodable_info =new nnet3::DecodableNnetSimpleLoopedInfo(*decodable_opts,
			asrShareResource->am_nnet);


		OnlineNnet2FeaturePipeline *feature_pipeline =new OnlineNnet2FeaturePipeline(*feature_info);

		SingleUtteranceNnet3Decoder *decoder= new SingleUtteranceNnet3Decoder(*decoder_opts, *asrShareResource->trans_model,
			*decodable_info,
			asrShareResource->wfst, feature_pipeline);



		asrShareOpt->decodable_info = decodable_info;
		asrShareOpt->decodable_opts = decodable_opts;
		asrShareOpt->decoder_opts = decoder_opts;
		asrShareOpt->endpoint_opts = endpoint_opts;
		asrShareOpt->feature_info = feature_info;
		asrShareOpt->feature_opts = feature_opts;



		return 0;
	}

}//namespace