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

/** \file     TAppDecCfg.h
    \brief    Decoder configuration class (header)
*/

#ifndef __TAPPDECCFG__
#define __TAPPDECCFG__

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "TLibCommon/CommonDef.h"
#include <vector>

//! \ingroup TAppDecoder
//! \{

// ====================================================================================================================
// Class definition
// ====================================================================================================================

/// Decoder configuration class
class TAppDecCfg
{
protected:
  Char*         m_pchBitstreamFile;                     ///< input bitstream file name
  Char*         m_pchReconFile;                         ///< output reconstruction file name
  Int           m_iSkipFrame;                           ///< counter for frames prior to the random access point to skip
  Int           m_outputBitDepth[MAX_NUM_CHANNEL_TYPE]; ///< bit depth used for writing output
#if RExt__COLOUR_SPACE_CONVERSIONS
  InputColourSpaceConversion m_outputColourSpaceConvert;
#endif

  Int           m_iMaxTemporalLayer;                  ///< maximum temporal layer to be decoded
  Int           m_decodedPictureHashSEIEnabled;       ///< Checksum(3)/CRC(2)/MD5(1)/disable(0) acting on decoded picture hash SEI message
#if RExt__M0042_NO_DISPLAY_SEI
  Bool          m_decodedNoDisplaySEIEnabled;         ///< Enable(true)/disable(false) writing only pictures that get displayed based on the no display SEI message
#endif
  std::vector<Int> m_targetDecLayerIdSet;             ///< set of LayerIds to be included in the sub-bitstream extraction process.
  Int           m_respectDefDispWindow;               ///< Only output content inside the default display window 
  
public:
  TAppDecCfg()
  : m_pchBitstreamFile(NULL)
  , m_pchReconFile(NULL) 
  , m_iSkipFrame(0)
#if RExt__COLOUR_SPACE_CONVERSIONS
  , m_outputColourSpaceConvert(IPCOLOURSPACE_UNCHANGED)
#endif
  , m_iMaxTemporalLayer(-1)
  , m_decodedPictureHashSEIEnabled(0)
#if RExt__M0042_NO_DISPLAY_SEI
  , m_decodedNoDisplaySEIEnabled(false)
#endif
  , m_respectDefDispWindow(0)
  {
    for (UInt channelTypeIndex = 0; channelTypeIndex < MAX_NUM_CHANNEL_TYPE; channelTypeIndex++)
      m_outputBitDepth[channelTypeIndex] = 0;
  }

  virtual ~TAppDecCfg() {}
  
  Bool  parseCfg        ( Int argc, Char* argv[] );   ///< initialize option class from configuration
};

//! \}

#endif


