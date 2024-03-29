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

/** \file     TEncEntropy.cpp
    \brief    entropy encoder class
*/

#include "TEncEntropy.h"
#include "TLibCommon/TypeDef.h"
#include "TLibCommon/TComSampleAdaptiveOffset.h"
#include "TLibCommon/TComTU.h"

#if RExt__ENVIRONMENT_VARIABLE_DEBUG_AND_TEST
#include "../TLibCommon/Debug.h"
static const Bool bDebugPredEnabled = DebugOptionList::DebugPred.getInt()!=0;
#endif

//! \ingroup TLibEncoder
//! \{

Void TEncEntropy::setEntropyCoder ( TEncEntropyIf* e, TComSlice* pcSlice )
{
  m_pcEntropyCoderIf = e;
  m_pcEntropyCoderIf->setSlice ( pcSlice );
}

Void TEncEntropy::encodeSliceHeader ( TComSlice* pcSlice )
{
  if (pcSlice->getSPS()->getUseSAO())
  {
    SAOParam *saoParam = pcSlice->getPic()->getPicSym()->getSaoParam();
    pcSlice->setSaoEnabledFlag       (saoParam->bSaoFlag[CHANNEL_TYPE_LUMA]);
    pcSlice->setSaoEnabledFlagChroma (saoParam->bSaoFlag[CHANNEL_TYPE_CHROMA]);
  }

  m_pcEntropyCoderIf->codeSliceHeader( pcSlice );
  return;
}

Void  TEncEntropy::encodeTilesWPPEntryPoint( TComSlice* pSlice )
{
  m_pcEntropyCoderIf->codeTilesWPPEntryPoint( pSlice );
}

Void TEncEntropy::encodeTerminatingBit      ( UInt uiIsLast )
{
  m_pcEntropyCoderIf->codeTerminatingBit( uiIsLast );

  return;
}

Void TEncEntropy::encodeSliceFinish()
{
  m_pcEntropyCoderIf->codeSliceFinish();
}

Void TEncEntropy::encodePPS( TComPPS* pcPPS )
{
  m_pcEntropyCoderIf->codePPS( pcPPS );
  return;
}

Void TEncEntropy::encodeSPS( TComSPS* pcSPS )
{
  m_pcEntropyCoderIf->codeSPS( pcSPS );
  return;
}

Void TEncEntropy::encodeCUTransquantBypassFlag( TComDataCU* pcCU, UInt uiAbsPartIdx, Bool bRD )
{
  if( bRD )
  {
    uiAbsPartIdx = 0;
  }
  m_pcEntropyCoderIf->codeCUTransquantBypassFlag( pcCU, uiAbsPartIdx );
}

Void TEncEntropy::encodeVPS( TComVPS* pcVPS )
{
  m_pcEntropyCoderIf->codeVPS( pcVPS );
  return;
}

Void TEncEntropy::encodeSkipFlag( TComDataCU* pcCU, UInt uiAbsPartIdx, Bool bRD )
{
  if ( pcCU->getSlice()->isIntra() )
  {
    return;
  }
  if( bRD )
  {
    uiAbsPartIdx = 0;
  }
  m_pcEntropyCoderIf->codeSkipFlag( pcCU, uiAbsPartIdx );
}

/** encode merge flag
 * \param pcCU
 * \param uiAbsPartIdx
 * \returns Void
 */
Void TEncEntropy::encodeMergeFlag( TComDataCU* pcCU, UInt uiAbsPartIdx )
{
  // at least one merge candidate exists
  m_pcEntropyCoderIf->codeMergeFlag( pcCU, uiAbsPartIdx );
}

/** encode merge index
 * \param pcCU
 * \param uiAbsPartIdx
 * \param bRD
 * \returns Void
 */
Void TEncEntropy::encodeMergeIndex( TComDataCU* pcCU, UInt uiAbsPartIdx, Bool bRD )
{
  if( bRD )
  {
    uiAbsPartIdx = 0;
    assert( pcCU->getPartitionSize(uiAbsPartIdx) == SIZE_2Nx2N );
  }
  m_pcEntropyCoderIf->codeMergeIndex( pcCU, uiAbsPartIdx );
}


/** encode prediction mode
 * \param pcCU
 * \param uiAbsPartIdx
 * \param bRD
 * \returns Void
 */
Void TEncEntropy::encodePredMode( TComDataCU* pcCU, UInt uiAbsPartIdx, Bool bRD )
{
  if( bRD )
  {
    uiAbsPartIdx = 0;
  }

  if ( pcCU->getSlice()->isIntra() )
  {
    return;
  }

#if RExt__N0256_INTRA_BLOCK_COPY
  if( pcCU->isIntraBC( uiAbsPartIdx ) )
  {
    return;
  }
#endif

  m_pcEntropyCoderIf->codePredMode( pcCU, uiAbsPartIdx );
}

// Split mode
Void TEncEntropy::encodeSplitFlag( TComDataCU* pcCU, UInt uiAbsPartIdx, UInt uiDepth, Bool bRD )
{
  if( bRD )
  {
    uiAbsPartIdx = 0;
  }

  m_pcEntropyCoderIf->codeSplitFlag( pcCU, uiAbsPartIdx, uiDepth );
}

/** encode partition size
 * \param pcCU
 * \param uiAbsPartIdx
 * \param uiDepth
 * \param bRD
 * \returns Void
 */
Void TEncEntropy::encodePartSize( TComDataCU* pcCU, UInt uiAbsPartIdx, UInt uiDepth, Bool bRD )
{
  if( bRD )
  {
    uiAbsPartIdx = 0;
  }

  m_pcEntropyCoderIf->codePartSize( pcCU, uiAbsPartIdx, uiDepth );
}

/** Encode I_PCM information.
 * \param pcCU pointer to CU
 * \param uiAbsPartIdx CU index
 * \param bRD flag indicating estimation or encoding
 * \returns Void
 */
Void TEncEntropy::encodeIPCMInfo( TComDataCU* pcCU, UInt uiAbsPartIdx, Bool bRD )
{
  if(!pcCU->getSlice()->getSPS()->getUsePCM()
    || pcCU->getWidth(uiAbsPartIdx) > (1<<pcCU->getSlice()->getSPS()->getPCMLog2MaxSize())
    || pcCU->getWidth(uiAbsPartIdx) < (1<<pcCU->getSlice()->getSPS()->getPCMLog2MinSize()))
  {
    return;
  }

  if( bRD )
  {
    uiAbsPartIdx = 0;
  }

  m_pcEntropyCoderIf->codeIPCMInfo ( pcCU, uiAbsPartIdx );

}


Void TEncEntropy::xEncodeTransform( Bool& bCodeDQP, TComTU &rTu )
{
//pcCU, absPartIdxCU, uiAbsPartIdx, uiDepth+1, uiTrIdx+1, quadrant,
  TComDataCU *pcCU=rTu.getCU();
  const UInt uiAbsPartIdx=rTu.GetAbsPartIdxTU();
  const UInt numValidComponent = pcCU->getPic()->getNumberValidComponents();
  const Bool bChroma = isChromaEnabled(pcCU->getPic()->getChromaFormat());
  const UInt uiTrIdx = rTu.GetTransformDepthRel();
  const UInt uiDepth = rTu.GetTransformDepthTotal();
#if RExt__ENVIRONMENT_VARIABLE_DEBUG_AND_TEST
  const Bool bDebugRQT=g_bFinalEncode && DebugOptionList::DebugRQT.getInt()!=0;
  if (bDebugRQT)
    printf("x..codeTransform: offsetLuma=%d offsetChroma=%d absPartIdx=%d, uiDepth=%d\n width=%d, height=%d, uiTrIdx=%d, uiInnerQuadIdx=%d\n",
           rTu.getCoefficientOffset(COMPONENT_Y), rTu.getCoefficientOffset(COMPONENT_Cb), uiAbsPartIdx, uiDepth, rTu.getRect(COMPONENT_Y).width, rTu.getRect(COMPONENT_Y).height, rTu.GetTransformDepthRel(), rTu.GetSectionNumber());
#endif
  const UInt uiSubdiv = pcCU->getTransformIdx( uiAbsPartIdx ) > uiTrIdx;// + pcCU->getDepth( uiAbsPartIdx ) > uiDepth;
  const UInt uiLog2TrafoSize = rTu.GetLog2LumaTrSize();


  UInt cbf[MAX_NUM_COMPONENT]={0,0,0};
  Bool bHaveACodedBlock=false;
  for(UInt ch=0; ch<numValidComponent; ch++)
  {
    cbf[ch] = pcCU->getCbf( uiAbsPartIdx, ComponentID(ch) , uiTrIdx );
    if (cbf[ch]) bHaveACodedBlock=true;
  }

#if RExt__N0256_INTRA_BLOCK_COPY
  if( pcCU->isIntra(uiAbsPartIdx) && pcCU->getPartitionSize(uiAbsPartIdx) == SIZE_NxN && uiDepth == pcCU->getDepth(uiAbsPartIdx) )
#else
  if( pcCU->getPredictionMode(uiAbsPartIdx) == MODE_INTRA && pcCU->getPartitionSize(uiAbsPartIdx) == SIZE_NxN && uiDepth == pcCU->getDepth(uiAbsPartIdx) )
#endif
  {
    assert( uiSubdiv );
  }
#if RExt__N0256_INTRA_BLOCK_COPY
  else if( pcCU->isInter(uiAbsPartIdx) && (pcCU->getPartitionSize(uiAbsPartIdx) != SIZE_2Nx2N) && uiDepth == pcCU->getDepth(uiAbsPartIdx) &&  (pcCU->getSlice()->getSPS()->getQuadtreeTUMaxDepthInter() == 1) )
#else
  else if( pcCU->getPredictionMode(uiAbsPartIdx) == MODE_INTER && (pcCU->getPartitionSize(uiAbsPartIdx) != SIZE_2Nx2N) && uiDepth == pcCU->getDepth(uiAbsPartIdx) &&  (pcCU->getSlice()->getSPS()->getQuadtreeTUMaxDepthInter() == 1) )
#endif
  {
    if ( uiLog2TrafoSize > pcCU->getQuadtreeTULog2MinSizeInCU(uiAbsPartIdx) )
    {
      assert( uiSubdiv );
    }
    else
    {
      assert(!uiSubdiv );
    }
  }
  else if( uiLog2TrafoSize > pcCU->getSlice()->getSPS()->getQuadtreeTULog2MaxSize() )
  {
    assert( uiSubdiv );
  }
  else if( uiLog2TrafoSize == pcCU->getSlice()->getSPS()->getQuadtreeTULog2MinSize() )
  {
    assert( !uiSubdiv );
  }
  else if( uiLog2TrafoSize == pcCU->getQuadtreeTULog2MinSizeInCU(uiAbsPartIdx) )
  {
    assert( !uiSubdiv );
  }
  else
  {
    assert( uiLog2TrafoSize > pcCU->getQuadtreeTULog2MinSizeInCU(uiAbsPartIdx) );
    m_pcEntropyCoderIf->codeTransformSubdivFlag( uiSubdiv, 5 - uiLog2TrafoSize );
  }

  const UInt uiTrDepthCurr = uiDepth - pcCU->getDepth( uiAbsPartIdx );
  const Bool bFirstCbfOfCU = uiTrDepthCurr == 0;

  for(UInt ch=COMPONENT_Cb; ch<numValidComponent; ch++)
  {
    const ComponentID compID=ComponentID(ch);
    if( bFirstCbfOfCU || rTu.ProcessingAllQuadrants(compID) )
    {
      if( bFirstCbfOfCU || pcCU->getCbf( uiAbsPartIdx, compID, uiTrDepthCurr - 1 ) )
      {
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
        m_pcEntropyCoderIf->codeQtCbf( rTu, compID, (uiSubdiv == 0) );
#else
        m_pcEntropyCoderIf->codeQtCbf( rTu, compID );
#endif
      }
    }
    else
    {
      assert( pcCU->getCbf( uiAbsPartIdx, compID, uiTrDepthCurr ) == pcCU->getCbf( uiAbsPartIdx, compID, uiTrDepthCurr - 1 ) );
    }
  }

  if( uiSubdiv )
  {
    TComTURecurse tuRecurseChild(rTu, true);
    do
    {
      xEncodeTransform( bCodeDQP, tuRecurseChild );
    }
    while (tuRecurseChild.nextSection(rTu));
  }
  else
  {
    {
      DTRACE_CABAC_VL( g_nSymbolCounter++ );
      DTRACE_CABAC_T( "\tTrIdx: abspart=" );
      DTRACE_CABAC_V( uiAbsPartIdx );
      DTRACE_CABAC_T( "\tdepth=" );
      DTRACE_CABAC_V( uiDepth );
      DTRACE_CABAC_T( "\ttrdepth=" );
      DTRACE_CABAC_V( pcCU->getTransformIdx( uiAbsPartIdx ) );
      DTRACE_CABAC_T( "\n" );
    }

#if RExt__N0256_INTRA_BLOCK_COPY
    if( !pcCU->isIntra(uiAbsPartIdx) && uiDepth == pcCU->getDepth( uiAbsPartIdx ) && (!bChroma || (!pcCU->getCbf( uiAbsPartIdx, COMPONENT_Cb, 0 ) && !pcCU->getCbf( uiAbsPartIdx, COMPONENT_Cr, 0 ) ) ) )
#else
    if( pcCU->getPredictionMode(uiAbsPartIdx) != MODE_INTRA && uiDepth == pcCU->getDepth( uiAbsPartIdx ) && (!bChroma || (!pcCU->getCbf( uiAbsPartIdx, COMPONENT_Cb, 0 ) && !pcCU->getCbf( uiAbsPartIdx, COMPONENT_Cr, 0 ) ) ) )
#endif
    {
      assert( pcCU->getCbf( uiAbsPartIdx, COMPONENT_Y, 0 ) );
      //      printf( "saved one bin! " );
    }
    else
    {
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
      m_pcEntropyCoderIf->codeQtCbf( rTu, COMPONENT_Y, true ); //luma CBF is always at the lowest level
#else
      m_pcEntropyCoderIf->codeQtCbf( rTu, COMPONENT_Y );
#endif
    }

    if ( bHaveACodedBlock )
    {
      // dQP: only for LCU once
      if ( pcCU->getSlice()->getPPS()->getUseDQP() )
      {
        if ( bCodeDQP )
        {
          encodeQP( pcCU, rTu.GetAbsPartIdxCU() );
          bCodeDQP = false;
        }
      }
      const UInt numValidComp=pcCU->getPic()->getNumberValidComponents();

      for(UInt ch=COMPONENT_Y; ch<numValidComp; ch++)
      {
        const ComponentID compID=ComponentID(ch);
        if (rTu.ProcessComponentSection(compID) && (cbf[compID] != 0))
        {
#if RExt__ENVIRONMENT_VARIABLE_DEBUG_AND_TEST
          if (bDebugRQT) printf("Call NxN for chan %d width=%d height=%d cbf=%d\n", compID, rTu.getRect(compID).width, rTu.getRect(compID).height, 1);
#endif

#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
          if (rTu.getRect(compID).width != rTu.getRect(compID).height)
          {
            //code two sub-TUs
            TComTURecurse subTUIterator(rTu, false, TComTU::VERTICAL_SPLIT, true, compID);

            do
            {
              const UChar subTUCBF = pcCU->getCbf(subTUIterator.GetAbsPartIdxTU(compID), compID, (uiTrIdx + 1));

              if (subTUCBF != 0)
              {
#if RExt__ENVIRONMENT_VARIABLE_DEBUG_AND_TEST
                if (bDebugRQT) printf("Call NxN for chan %d width=%d height=%d cbf=%d\n", compID, subTUIterator.getRect(compID).width, subTUIterator.getRect(compID).height, 1);
#endif
                m_pcEntropyCoderIf->codeCoeffNxN( subTUIterator, (pcCU->getCoeff(compID) + subTUIterator.getCoefficientOffset(compID)), compID );
              }
            }
            while (subTUIterator.nextSection(rTu));
          }
          else
          {
#endif
            m_pcEntropyCoderIf->codeCoeffNxN( rTu, (pcCU->getCoeff(compID) + rTu.getCoefficientOffset(compID)), compID );
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
          }
#endif
        }
      }
    }
  }
}


// Intra direction for Luma
Void TEncEntropy::encodeIntraDirModeLuma  ( TComDataCU* pcCU, UInt absPartIdx, Bool isMultiplePU )
{
  m_pcEntropyCoderIf->codeIntraDirLumaAng( pcCU, absPartIdx , isMultiplePU);
}


// Intra direction for Chroma
Void TEncEntropy::encodeIntraDirModeChroma( TComDataCU* pcCU, UInt uiAbsPartIdx )
{
  m_pcEntropyCoderIf->codeIntraDirChroma( pcCU, uiAbsPartIdx );

#if RExt__ENVIRONMENT_VARIABLE_DEBUG_AND_TEST
  if (bDebugPredEnabled && g_bFinalEncode)
  {
    UInt cdir=pcCU->getIntraDir(CHANNEL_TYPE_CHROMA, uiAbsPartIdx);
    if (cdir==36) cdir=pcCU->getIntraDir(CHANNEL_TYPE_LUMA, uiAbsPartIdx);
    printf("coding chroma Intra dir: %d, uiAbsPartIdx: %d, luma dir: %d\n", cdir, uiAbsPartIdx, pcCU->getIntraDir(CHANNEL_TYPE_LUMA, uiAbsPartIdx));
  }
#endif
}

#if RExt__N0256_INTRA_BLOCK_COPY
Void TEncEntropy::encodeIntraBCFlag( TComDataCU* pcCU, UInt uiAbsPartIdx, Bool bRD )
{
  if( bRD )
  {
    uiAbsPartIdx = 0;
  }
  m_pcEntropyCoderIf->codeIntraBCFlag( pcCU, uiAbsPartIdx );
}

Void TEncEntropy::encodeIntraBC( TComDataCU* pcCU, UInt uiAbsPartIdx )
{
  m_pcEntropyCoderIf->codeIntraBC( pcCU, uiAbsPartIdx );
}
#endif

Void TEncEntropy::encodePredInfo( TComDataCU* pcCU, UInt uiAbsPartIdx )
{
#if RExt__N0256_INTRA_BLOCK_COPY
  assert ( !pcCU->isIntraBC( uiAbsPartIdx ) );
#endif

  if( pcCU->isIntra( uiAbsPartIdx ) )                                 // If it is Intra mode, encode intra prediction mode.
  {
    encodeIntraDirModeLuma  ( pcCU, uiAbsPartIdx,true );
    if (pcCU->getPic()->getChromaFormat()!=CHROMA_400)
    {
      encodeIntraDirModeChroma( pcCU, uiAbsPartIdx );

      if (enable4ChromaPUsInIntraNxNCU(pcCU->getPic()->getChromaFormat()) && pcCU->getPartitionSize( uiAbsPartIdx )==SIZE_NxN)
      {
        UInt uiPartOffset = ( pcCU->getPic()->getNumPartInCU() >> ( pcCU->getDepth(uiAbsPartIdx) << 1 ) ) >> 2;
        encodeIntraDirModeChroma( pcCU, uiAbsPartIdx + uiPartOffset   );
        encodeIntraDirModeChroma( pcCU, uiAbsPartIdx + uiPartOffset*2 );
        encodeIntraDirModeChroma( pcCU, uiAbsPartIdx + uiPartOffset*3 );
      }
    }
  }
  else                                                                // if it is Inter mode, encode motion vector and reference index
  {
    encodePUWise( pcCU, uiAbsPartIdx );
  }
}

/** encode motion information for every PU block
 * \param pcCU
 * \param uiAbsPartIdx
 * \param bRD
 * \returns Void
 */
Void TEncEntropy::encodePUWise( TComDataCU* pcCU, UInt uiAbsPartIdx )
{
#if RExt__ENVIRONMENT_VARIABLE_DEBUG_AND_TEST
  const Bool bDebugPred = bDebugPredEnabled && g_bFinalEncode;
#endif

  PartSize ePartSize = pcCU->getPartitionSize( uiAbsPartIdx );
  UInt uiNumPU = ( ePartSize == SIZE_2Nx2N ? 1 : ( ePartSize == SIZE_NxN ? 4 : 2 ) );
  UInt uiDepth = pcCU->getDepth( uiAbsPartIdx );
  UInt uiPUOffset = ( g_auiPUOffset[UInt( ePartSize )] << ( ( pcCU->getSlice()->getSPS()->getMaxCUDepth() - uiDepth ) << 1 ) ) >> 4;

  for ( UInt uiPartIdx = 0, uiSubPartIdx = uiAbsPartIdx; uiPartIdx < uiNumPU; uiPartIdx++, uiSubPartIdx += uiPUOffset )
  {
    encodeMergeFlag( pcCU, uiSubPartIdx );
    if ( pcCU->getMergeFlag( uiSubPartIdx ) )
    {
      encodeMergeIndex( pcCU, uiSubPartIdx );
#if RExt__ENVIRONMENT_VARIABLE_DEBUG_AND_TEST
      if (bDebugPred)
      {
        std::cout << "Coded merge flag, CU absPartIdx: " << uiAbsPartIdx << " PU(" << uiPartIdx << ") absPartIdx: " << uiSubPartIdx;
        std::cout << " merge index: " << (UInt)pcCU->getMergeIndex(uiSubPartIdx) << std::endl;
      }
#endif
    }
    else
    {
      encodeInterDirPU( pcCU, uiSubPartIdx );
      for ( UInt uiRefListIdx = 0; uiRefListIdx < 2; uiRefListIdx++ )
      {
        if ( pcCU->getSlice()->getNumRefIdx( RefPicList( uiRefListIdx ) ) > 0 )
        {
          encodeRefFrmIdxPU ( pcCU, uiSubPartIdx, RefPicList( uiRefListIdx ) );
          encodeMvdPU       ( pcCU, uiSubPartIdx, RefPicList( uiRefListIdx ) );
          encodeMVPIdxPU    ( pcCU, uiSubPartIdx, RefPicList( uiRefListIdx ) );
#if RExt__ENVIRONMENT_VARIABLE_DEBUG_AND_TEST
          if (bDebugPred)
          {
            std::cout << "refListIdx: " << uiRefListIdx << std::endl;
            std::cout << "MVD horizontal: " << pcCU->getCUMvField(RefPicList(uiRefListIdx))->getMvd( uiAbsPartIdx ).getHor() << std::endl;
            std::cout << "MVD vertical:   " << pcCU->getCUMvField(RefPicList(uiRefListIdx))->getMvd( uiAbsPartIdx ).getVer() << std::endl;
            std::cout << "MVPIdxPU: " << pcCU->getMVPIdx(RefPicList( uiRefListIdx ), uiSubPartIdx) << std::endl;
            std::cout << "InterDir: " << (UInt)pcCU->getInterDir(uiSubPartIdx) << std::endl;
          }
#endif
        }
      }
    }
  }

  return;
}

Void TEncEntropy::encodeInterDirPU( TComDataCU* pcCU, UInt uiAbsPartIdx )
{
  if ( !pcCU->getSlice()->isInterB() )
  {
    return;
  }

  m_pcEntropyCoderIf->codeInterDir( pcCU, uiAbsPartIdx );

  return;
}

/** encode reference frame index for a PU block
 * \param pcCU
 * \param uiAbsPartIdx
 * \param eRefList
 * \returns Void
 */
Void TEncEntropy::encodeRefFrmIdxPU( TComDataCU* pcCU, UInt uiAbsPartIdx, RefPicList eRefList )
{
#if RExt__N0256_INTRA_BLOCK_COPY
  assert( pcCU->isInter( uiAbsPartIdx ) );
#else
  assert( !pcCU->isIntra( uiAbsPartIdx ) );
#endif

  if ( ( pcCU->getSlice()->getNumRefIdx( eRefList ) == 1 ) )
  {
    return;
  }

  if ( pcCU->getInterDir( uiAbsPartIdx ) & ( 1 << eRefList ) )
  {
    m_pcEntropyCoderIf->codeRefFrmIdx( pcCU, uiAbsPartIdx, eRefList );
  }

  return;
}

/** encode motion vector difference for a PU block
 * \param pcCU
 * \param uiAbsPartIdx
 * \param eRefList
 * \returns Void
 */
Void TEncEntropy::encodeMvdPU( TComDataCU* pcCU, UInt uiAbsPartIdx, RefPicList eRefList )
{
#if RExt__N0256_INTRA_BLOCK_COPY
  assert( pcCU->isInter( uiAbsPartIdx ) );
#else
  assert( !pcCU->isIntra( uiAbsPartIdx ) );
#endif

  if ( pcCU->getInterDir( uiAbsPartIdx ) & ( 1 << eRefList ) )
  {
    m_pcEntropyCoderIf->codeMvd( pcCU, uiAbsPartIdx, eRefList );
  }
  return;
}

Void TEncEntropy::encodeMVPIdxPU( TComDataCU* pcCU, UInt uiAbsPartIdx, RefPicList eRefList )
{
  if ( (pcCU->getInterDir( uiAbsPartIdx ) & ( 1 << eRefList )) )
  {
    m_pcEntropyCoderIf->codeMVPIdx( pcCU, uiAbsPartIdx, eRefList );
  }

  return;
}

#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
Void TEncEntropy::encodeQtCbf( TComTU &rTu, const ComponentID compID, const Bool lowestLevel )
{
  m_pcEntropyCoderIf->codeQtCbf( rTu, compID, lowestLevel );
}
#else
Void TEncEntropy::encodeQtCbf( TComTU &rTu, const ComponentID compID )
{
  m_pcEntropyCoderIf->codeQtCbf( rTu, compID );
}
#endif

Void TEncEntropy::encodeTransformSubdivFlag( UInt uiSymbol, UInt uiCtx )
{
  m_pcEntropyCoderIf->codeTransformSubdivFlag( uiSymbol, uiCtx );
}

Void TEncEntropy::encodeQtRootCbf( TComDataCU* pcCU, UInt uiAbsPartIdx )
{
  m_pcEntropyCoderIf->codeQtRootCbf( pcCU, uiAbsPartIdx );
}

#if RExt__BACKWARDS_COMPATIBILITY_HM_TICKET_986
Void TEncEntropy::encodeQtCbfZero( TComTU &rTu, const ChannelType chType, const Bool useAdjustedDepth )
{
  //NOTE: RExt - In HM, this function is called in multiple ways, which may not be intended.
  //      In xEstimateResidualQT, when coding the chroma channel, it is called with both TrDepth and TrDepthC (adjusted for step-up cases).
  m_pcEntropyCoderIf->codeQtCbfZero( rTu, chType, useAdjustedDepth );
}
#else
Void TEncEntropy::encodeQtCbfZero( TComTU &rTu, const ChannelType chType )
{
  m_pcEntropyCoderIf->codeQtCbfZero( rTu, chType );
}
#endif

Void TEncEntropy::encodeQtRootCbfZero( TComDataCU* pcCU )
{
  m_pcEntropyCoderIf->codeQtRootCbfZero( pcCU );
}

// dQP
Void TEncEntropy::encodeQP( TComDataCU* pcCU, UInt uiAbsPartIdx, Bool bRD )
{
  if( bRD )
  {
    uiAbsPartIdx = 0;
  }

  if ( pcCU->getSlice()->getPPS()->getUseDQP() )
  {
    m_pcEntropyCoderIf->codeDeltaQP( pcCU, uiAbsPartIdx );
  }
}


// texture

/** encode coefficients
 * \param pcCU
 * \param uiAbsPartIdx
 * \param uiDepth
 * \param uiWidth
 * \param uiHeight
 */
Void TEncEntropy::encodeCoeff( TComDataCU* pcCU, UInt uiAbsPartIdx, UInt uiDepth, Bool& bCodeDQP )
{
#if RExt__ENVIRONMENT_VARIABLE_DEBUG_AND_TEST
  const Bool bDebugRQT=g_bFinalEncode && DebugOptionList::DebugRQT.getInt()!=0;
#endif

  if( pcCU->isIntra(uiAbsPartIdx) )
  {
    if (false)
    {
      DTRACE_CABAC_VL( g_nSymbolCounter++ )
      DTRACE_CABAC_T( "\tdecodeTransformIdx()\tCUDepth=" )
      DTRACE_CABAC_V( uiDepth )
      DTRACE_CABAC_T( "\n" )
    }
  }
  else
  {
    if( !(pcCU->getMergeFlag( uiAbsPartIdx ) && pcCU->getPartitionSize(uiAbsPartIdx) == SIZE_2Nx2N ) )
    {
      m_pcEntropyCoderIf->codeQtRootCbf( pcCU, uiAbsPartIdx );
    }
    if ( !pcCU->getQtRootCbf( uiAbsPartIdx ) )
    {
      return;
    }
  }

  TComTURecurse tuRecurse(pcCU, uiAbsPartIdx, uiDepth);
#if RExt__ENVIRONMENT_VARIABLE_DEBUG_AND_TEST
  if (bDebugRQT) printf("..codeCoeff: uiAbsPartIdx=%d, PU format=%d, 2Nx2N=%d, NxN=%d\n", uiAbsPartIdx, pcCU->getPartitionSize(uiAbsPartIdx), SIZE_2Nx2N, SIZE_NxN);
#endif

  xEncodeTransform( bCodeDQP, tuRecurse );
}

Void TEncEntropy::encodeCoeffNxN( TComTU &rTu, TCoeff* pcCoef, const ComponentID compID)
{
  TComDataCU *pcCU = rTu.getCU();

  if (pcCU->getCbf(rTu.GetAbsPartIdxTU(), compID , rTu.GetTransformDepthRel()) != 0)
  {
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
    if (rTu.getRect(compID).width != rTu.getRect(compID).height)
    {
      //code two sub-TUs
      TComTURecurse subTUIterator(rTu, false, TComTU::VERTICAL_SPLIT, true, compID);

      const UInt subTUSize = subTUIterator.getRect(compID).width * subTUIterator.getRect(compID).height;

      do
      {
        const UChar subTUCBF = pcCU->getCbf(subTUIterator.GetAbsPartIdxTU(compID), compID, (subTUIterator.GetTransformDepthRel() + 1));

        if (subTUCBF != 0)
        {
          m_pcEntropyCoderIf->codeCoeffNxN( subTUIterator, (pcCoef + (subTUIterator.GetSectionNumber() * subTUSize)), compID);
        }
      }
      while (subTUIterator.nextSection(rTu));
    }
    else
    {
#endif
      m_pcEntropyCoderIf->codeCoeffNxN(rTu, pcCoef, compID);
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
    }
#endif
  }
}

Void TEncEntropy::estimateBit (estBitsSbacStruct* pcEstBitsSbac, Int width, Int height, const ChannelType chType)
{
#if (RExt__SQUARE_TRANSFORM_CHROMA_422 != 0)
  const UInt heightAtEntropyCoding = (width != height) ? (height >> 1) : height;

  m_pcEntropyCoderIf->estBit ( pcEstBitsSbac, width, heightAtEntropyCoding, chType );
#else
  m_pcEntropyCoderIf->estBit ( pcEstBitsSbac, width, height, chType );
#endif
}

/** Encode SAO Offset
 * \param  saoLcuParam SAO LCU paramters
 */
Void TEncEntropy::encodeSaoOffset(SaoLcuParam* saoLcuParam, const ComponentID compID )
{
  UInt uiSymbol;
  Int i;

  uiSymbol = saoLcuParam->typeIdx + 1;

  if (compID != COMPONENT_Cr)
  {
    m_pcEntropyCoderIf->codeSaoTypeIdx(uiSymbol);
  }

  if (uiSymbol)
  {
    if (saoLcuParam->typeIdx < 4 && compID != COMPONENT_Cr)
    {
      saoLcuParam->subTypeIdx = saoLcuParam->typeIdx;
    }

    Int offsetTh = 1 << min(g_bitDepth[toChannelType(compID)] - 5,5);

    if( saoLcuParam->typeIdx == SAO_BO )
    {
      for( i=0; i< saoLcuParam->length; i++)
      {
        UInt absOffset = ( (saoLcuParam->offset[i] < 0) ? -saoLcuParam->offset[i] : saoLcuParam->offset[i]);
        m_pcEntropyCoderIf->codeSaoMaxUvlc(absOffset, offsetTh-1);
      }
      for( i=0; i< saoLcuParam->length; i++)
      {
        if (saoLcuParam->offset[i] != 0)
        {
          UInt sign = (saoLcuParam->offset[i] < 0) ? 1 : 0 ;
          m_pcEntropyCoderIf->codeSAOSign(sign);
        }
      }
      uiSymbol = (UInt) (saoLcuParam->subTypeIdx);
      m_pcEntropyCoderIf->codeSaoUflc(5, uiSymbol);
    }
    else if( saoLcuParam->typeIdx < 4 )
    {
      m_pcEntropyCoderIf->codeSaoMaxUvlc( saoLcuParam->offset[0], offsetTh-1);
      m_pcEntropyCoderIf->codeSaoMaxUvlc( saoLcuParam->offset[1], offsetTh-1);
      m_pcEntropyCoderIf->codeSaoMaxUvlc(-saoLcuParam->offset[2], offsetTh-1);
      m_pcEntropyCoderIf->codeSaoMaxUvlc(-saoLcuParam->offset[3], offsetTh-1);

      if (compID != COMPONENT_Cr)
      {
        uiSymbol = (UInt) (saoLcuParam->subTypeIdx);
        m_pcEntropyCoderIf->codeSaoUflc(2, uiSymbol);
      }
    }
  }
}


/** Encode SAO unit interleaving
* \param  rx
* \param  ry
* \param  pSaoParam
* \param  pcCU
* \param  iCUAddrInSlice
* \param  iCUAddrUpInSlice
* \param  bLFCrossSliceBoundaryFlag
 */

Void TEncEntropy::encodeSaoUnitInterleaving(ComponentID compID, Bool saoFlag, Int rx, Int ry, SaoLcuParam* saoLcuParam, Int cuAddrInSlice, Int cuAddrUpInSlice, Int allowMergeLeft, Int allowMergeUp)
{
  if (saoFlag)
  {
    if (rx>0 && cuAddrInSlice!=0 && allowMergeLeft)
    {
      m_pcEntropyCoderIf->codeSaoMerge(saoLcuParam->mergeLeftFlag);
    }
    else
    {
      saoLcuParam->mergeLeftFlag = 0;
    }
    if (saoLcuParam->mergeLeftFlag == 0)
    {
      if ( (ry > 0) && (cuAddrUpInSlice>=0) && allowMergeUp )
      {
        m_pcEntropyCoderIf->codeSaoMerge(saoLcuParam->mergeUpFlag);
      }
      else
      {
        saoLcuParam->mergeUpFlag = 0;
      }
      if (!saoLcuParam->mergeUpFlag)
      {
        encodeSaoOffset(saoLcuParam, compID);
      }
    }
  }
}


Int TEncEntropy::countNonZeroCoeffs( TCoeff* pcCoef, UInt uiSize )
{
  Int count = 0;

  for ( Int i = 0; i < uiSize; i++ )
  {
    count += pcCoef[i] != 0;
  }

  return count;
}

/** encode quantization matrix
 * \param scalingList quantization matrix information
 */
Void TEncEntropy::encodeScalingList( TComScalingList* scalingList )
{
  m_pcEntropyCoderIf->codeScalingList( scalingList );
}

//! \}
