/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.  
 *
 * Copyright (c) 2010-2013, ITU/ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of the ITU/ISO/IEC nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 \file     TEncSampleAdaptiveOffset.h
 \brief    estimation part of sample adaptive offset class (header)
 */

#ifndef __TENCSAMPLEADAPTIVEOFFSET__
#define __TENCSAMPLEADAPTIVEOFFSET__

#include "TLibCommon/TComSampleAdaptiveOffset.h"
#include "TLibCommon/TComPic.h"

#include "TEncEntropy.h"
#include "TEncSbac.h"
#include "TLibCommon/TComBitCounter.h"

//! \ingroup TLibEncoder
//! \{

// ====================================================================================================================
// Class definition
// ====================================================================================================================

class TEncSampleAdaptiveOffset : public TComSampleAdaptiveOffset
{
private:
  TEncEntropy*      m_pcEntropyCoder;
  TEncSbac***       m_pppcRDSbacCoder;              ///< for CABAC
  TEncSbac*         m_pcRDGoOnSbacCoder;
#if FAST_BIT_EST
  TEncBinCABACCounter*** m_pppcBinCoderCABAC;            ///< temporal CABAC state storage for RD computation
#else
  TEncBinCABAC***   m_pppcBinCoderCABAC;            ///< temporal CABAC state storage for RD computation
#endif
  
  Int64  ***m_iCount;      //[MAX_NUM_SAO_PART][MAX_NUM_SAO_TYPE][MAX_NUM_SAO_CLASS]; 
  Int64  ***m_iOffset;     //[MAX_NUM_SAO_PART][MAX_NUM_SAO_TYPE][MAX_NUM_SAO_CLASS]; 
  Int64  ***m_iOffsetOrg;  //[MAX_NUM_SAO_PART][MAX_NUM_SAO_TYPE]; 
  Int64  ****m_count_PreDblk;      //[LCU][YCbCr][MAX_NUM_SAO_TYPE][MAX_NUM_SAO_CLASS]; 
  Int64  ****m_offsetOrg_PreDblk;  //[LCU][YCbCr][MAX_NUM_SAO_TYPE][MAX_NUM_SAO_CLASS]; 
  Int64  **m_iRate;        //[MAX_NUM_SAO_PART][MAX_NUM_SAO_TYPE]; 
  Int64  **m_iDist;        //[MAX_NUM_SAO_PART][MAX_NUM_SAO_TYPE]; 
  Double **m_dCost;        //[MAX_NUM_SAO_PART][MAX_NUM_SAO_TYPE]; 
  Double *m_dCostPartBest; //[MAX_NUM_SAO_PART]; 
  Int64  *m_iDistOrg;      //[MAX_NUM_SAO_PART]; 
  Int    *m_iTypePartBest; //[MAX_NUM_SAO_PART]; 
  Int     m_iOffsetTh[MAX_NUM_CHANNEL_TYPE];
  Bool    m_bUseSBACRD;
#if SAO_ENCODING_CHOICE
#if SAO_ENCODING_CHOICE_CHROMA
  std::vector<Double> m_depthSaoRate; // [0]=(depth0, channel0), [1]=(depth0, channel 1), [MAX_NUM_CHANNEL_TYPE]=(depth 1, channel 0), etc
#else
  Double  m_depth0SaoRate;
#endif
#endif

public:
  TEncSampleAdaptiveOffset         ();
  virtual ~TEncSampleAdaptiveOffset();

  Void startSaoEnc( TComPic* pcPic, TEncEntropy* pcEntropyCoder, TEncSbac*** pppcRDSbacCoder, TEncSbac* pcRDGoOnSbacCoder);
  Void endSaoEnc();
  Void resetStats();
#if SAO_CHROMA_LAMBDA
#if RExt__BACKWARDS_COMPATIBILITY_HM_TICKET_990_SAO
#if SAO_ENCODING_CHOICE
  Void SAOProcess(SAOParam *pcSaoParam, Double dLambda, Double dLambdaChroma, Int depth);
#else
  Void SAOProcess(SAOParam *pcSaoParam, Double dLambda, Double dLambdaChroma);
#endif
#else
#if SAO_ENCODING_CHOICE
  Void SAOProcess(SAOParam *pcSaoParam, const Double dLambdas[MAX_NUM_COMPONENT], Int depth);
#else
  Void SAOProcess(SAOParam *pcSaoParam, const Double dLambdas[MAX_NUM_COMPONENT]);
#endif
#endif
#else
  Void SAOProcess(SAOParam *pcSaoParam, Double dLambda);
#endif

  Void runQuadTreeDecision(SAOQTPart *psQTPart, Int iPartIdx, Double &dCostFinal, Int iMaxLevel, Double dLambda, ComponentID yCbCr);
  Void rdoSaoOnePart(SAOQTPart *psQTPart, Int iPartIdx, Double dLambda, ComponentID yCbCr);
  
  Void disablePartTree(SAOQTPart *psQTPart, Int iPartIdx);
  Void getSaoStats(SAOQTPart *psQTPart, ComponentID iYCbCr);
  Void calcSaoStatsCu(Int iAddr, Int iPartIdx, ComponentID iYCbCr);
  Void calcSaoStatsBlock( Pel* pRecStart, Pel* pOrgStart, Int stride, Int64** ppStats, Int64** ppCount, UInt width, UInt height, Bool* pbBorderAvail, ComponentID iYCbCr);
  Void calcSaoStatsCuOrg(Int iAddr, Int iPartIdx, ComponentID iYCbCr);
  Void calcSaoStatsCu_BeforeDblk( TComPic* pcPic );
  Void destroyEncBuffer();
  Void createEncBuffer();
  Void assignSaoUnitSyntax(SaoLcuParam* saoLcuParam,  SAOQTPart* saoPart, Bool &oneUnitFlag);
  Void checkMerge(SaoLcuParam * lcuParamCurr,SaoLcuParam * lcuParamCheck, Int dir);
#if RExt__BACKWARDS_COMPATIBILITY_HM_TICKET_990_SAO
#if SAO_ENCODING_CHOICE
  Void rdoSaoUnitAll(SAOParam *saoParam, Double lambda, Double lambdaChroma, Int depth);
#else
  Void rdoSaoUnitAll(SAOParam *saoParam, Double lambda, Double lambdaChroma);
#endif
#else
#if SAO_ENCODING_CHOICE
  Void rdoSaoUnitAll(SAOParam *saoParam, const Double lambdas[MAX_NUM_COMPONENT], Int depth);
#else
  Void rdoSaoUnitAll(SAOParam *saoParam, const Double lambdas[MAX_NUM_COMPONENT]);
#endif
#endif
  Void saoComponentParamDist(Int allowMergeLeft, Int allowMergeUp, SAOParam *saoParam, Int addr, Int addrUp, Int addrLeft, ComponentID yCbCr, Double lambda, SaoLcuParam *compSaoParam, Double *distortion);
#if RExt__BACKWARDS_COMPATIBILITY_HM_TICKET_990_SAO
  Void sao2ChromaParamDist(Int allowMergeLeft, Int allowMergeUp, SAOParam *saoParam, Int addr, Int addrUp, Int addrLeft, Double lambda, SaoLcuParam *cbSaoParam, SaoLcuParam *crSaoParam, Double *distortion);
#else
  Void sao2ChromaParamDist(Int allowMergeLeft, Int allowMergeUp, SAOParam *saoParam, Int addr, Int addrUp, Int addrLeft, const Double lambdas[MAX_NUM_COMPONENT], SaoLcuParam *cbSaoParam, SaoLcuParam *crSaoParam, Double *distortion);
#endif
  inline Int64 estSaoDist(Int64 count, Int64 offset, Int64 offsetOrg, Int shift);
  inline Int64 estIterOffset(Int typeIdx, Int classIdx, Double lambda, Int64 offsetInput, Int64 count, Int64 offsetOrg, Int shift, Int bitIncrease, Int *currentDistortionTableBo, Double *currentRdCostTableBo, Int offsetTh );
#if RExt__BACKWARDS_COMPATIBILITY_HM_TICKET_987
  inline Int64 estSaoTypeDist(Int compPartIdx, Int typeIdx, Int shift, Double lambda, Int *currentDistortionTableBo, Double *currentRdCostTableBo);
#else
  inline Int64 estSaoTypeDist(const ComponentID compID, Int compPartIdx, Int typeIdx, Int shift, Double lambda, Int *currentDistortionTableBo, Double *currentRdCostTableBo);
  inline Int64 estSaoTypeDist(const ComponentID compID, Int typeIdx, Int shift, Double lambda, Int *currentDistortionTableBo, Double *currentRdCostTableBo);
#endif
  Void setMaxNumOffsetsPerPic(Int iVal) {m_maxNumOffsetsPerPic = iVal; }
  Int  getMaxNumOffsetsPerPic() {return m_maxNumOffsetsPerPic; }
};

//! \}

#endif
