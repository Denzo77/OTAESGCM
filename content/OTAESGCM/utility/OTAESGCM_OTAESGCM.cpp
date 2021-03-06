/*
The OpenTRV project licenses this file to you
under the Apache Licence, Version 2.0 (the "Licence");
you may not use this file except in compliance
with the Licence. You may obtain a copy of the Licence at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the Licence is distributed on an
"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
KIND, either express or implied. See the Licence for the
specific language governing permissions and limitations
under the Licence.

Author(s) / Copyright (s): Deniz Erbilgin 2015--2017
                           Damon Hart-Davis 2015--2017
*/

/* OpenTRV OTAESGCM microcontroller-/IoT- friendly AES(128)-GCM implementation. */


#include <string.h>

#include "OTAESGCM_OTAESGCM.h"

#if !defined(ARDUINO_ARCH_AVR)
#include <stdio.h>
#endif


// Use namespaces to help avoid collisions.
namespace OTAESGCM
    {

/*********************************************************
 * @todo    description of GCM Algorithm here
 *********************************************************
 *
 *
 *********************************************************/

/********************** Includes *************************/
/******************* Global Variables ********************/
//
/******************* Private Variables *******************/

/******************* Private Functions *******************/
/**
 * @note    xor_block
 * @brief    xor on 128bit block.
 * @param    dest:    pointer to destination
 * @param    src:    pointer to source
 */
static void xorBlock(uint8_t *dest, const uint8_t *src)
{
    for(uint8_t i = 0; i < AES128GCM_BLOCK_SIZE; i++){
        *dest++ ^= *src++;
    }
}

/**
 * @note    shift_block_right
 * @brief    bitshifts 128bit block (16 byte array) right once
 * @param    block:    pointer to block to shift
 * @note    I separated the pointer decrement from the bit shift as it was
 *             mangling the first byte
 */
static void shiftBlockRight(uint8_t *block)
{
    block += 15;

    // bitshift LSB (last byte in array)
    *block = *block >> 1;
    block--;

    // loop through remaining bytes
    for (uint8_t i = 0; i < AES128GCM_BLOCK_SIZE-1; i++) {
        // if lsb is set, set msb of next byte in array
        if(*block & 0x01) *(block + 1) |= 0x80;
        // bit shift byte
        *block = *block >> 1;
        block--;
    }
}

/**
 * @brief   checks if tags match
 * @param   tag1        pointer to array containing tag1
 * @param   tag2        pointer to array containing tag2
 * @retval  returns 0 if tags match. All other values are a fail.
 */
static uint8_t checkTag(const uint8_t *tag1, const uint8_t *tag2)
{
    uint8_t result = 0;

    // Compare tags: f any byte pair fails to match this will set bits in result.
    // This method runtime does not depend on where the match is,
    // which will help avoid some side-channel attacks based on timing.
    for (uint8_t i = 0; i < AES128GCM_TAG_SIZE; i++) {
        result |= *tag1 ^ *tag2;
        tag1++;
        tag2++;
    }
    return result;
}

/**
 * @note    gf_mult
 * @brief    Performs multiplications in 128 bit galois bit field
 * @todo    learn how multiplication algorithm works
 * @param    x: pointer to input 1
 * @param    y: pointer to input 2
 * @param    result:    pointer to array to put result in
 * @note    output straight to *x and save on a memcpy loop?
 */
static void gFieldMultiply(GGBWS::GHASHWorkspace * const workspace, const uint8_t *x, const uint8_t *y)
{
    // init result to 0s and copy y to temp
    memcpy(workspace->gFieldMultiplyTmp, y, AES128GCM_BLOCK_SIZE);
    memset(workspace->ghashTmp, 0, AES128GCM_BLOCK_SIZE);
    
        // multiplication algorithm
    for (uint8_t i = 0; i < AES128GCM_BLOCK_SIZE; i++) {
        for (uint8_t j = 0; j < 8; j++) {

            if (x[i] & (1 << (7 - j))) {
                /* Z_(i + 1) = Z_i XOR V_i */
                xorBlock(workspace->ghashTmp, workspace->gFieldMultiplyTmp);
            }
            // if temp is odd, do something?
            if (workspace->gFieldMultiplyTmp[15] & 0x01) {
                /* V_(i + 1) = (V_i >> 1) XOR R */
                shiftBlockRight(workspace->gFieldMultiplyTmp);
                /* R = 11100001 || 0^120 */
                workspace->gFieldMultiplyTmp[0] ^= 0xe1;
            } else {
                /* V_(i + 1) = V_i >> 1 */
                shiftBlockRight(workspace->gFieldMultiplyTmp);
            }
        }
    }
}

/**
 * @note    inc32
 * @brief    increments the rightmost 32 bits (4 bytes) of block, %(2^32)
 * @param    pBlock      16 byte array to perform operation on
 */
static void incr32(uint8_t *pBlock)
{
    // go to end of array
    pBlock += 15;

    // loop through last 4 elements
    for (uint8_t i = 0; i < 4; i++) {
        // increment current byte
        *pBlock = *pBlock + 1;
        // return if no overflow, otherwise move to next byte
        if(*pBlock) return;
        else pBlock--;
    }
}

#if defined(OTAESGCM_ALLOW_UNPADDED)
//**************** MAIN ENCRYPTION FUNCTIONS *************
/**
 * @note    aes_gctr
 * @brief   performs gcntr operation for encryption
 * @param   pInput          pointer to input data (need not be block multiple)
 * @param   inputLength     length of input array
 * @param   pKey            pointer to 128 bit AES key
 * @param   pICB            initial counter block J0
 * @param   pOutput         pointer to output data. length inputLength rounded up to 16.
 */
static void GCTR(OTAES128E * const ap, GGBWS::GCTRWorkspace * const workspace,
                    const uint8_t *pInput, const uint8_t inputLength, const uint8_t *pKey,
                    const uint8_t *pCtrBlock, uint8_t *pOutput)
{
    const uint8_t *xpos = pInput;
    uint8_t *ypos = pOutput;

    // exit function if no input data
    if (inputLength == 0) return;

    // calculate number of full blocks to cipher
    const uint8_t n = inputLength / 16;

    // copy ICB to ctrBlock
    memcpy(workspace->ctrBlock, pCtrBlock, AES128GCM_BLOCK_SIZE);

    // for full blocks
    for (uint8_t i = 0; i < n; i++) {
        // cipher counterblock and combine with input
        ap->blockEncrypt(workspace->ctrBlock, pKey, ypos);
        xorBlock(ypos, xpos);

        // increment pointers to next block
        xpos += AES128GCM_BLOCK_SIZE;
        ypos += AES128GCM_BLOCK_SIZE;

        // increment counter
        incr32(workspace->ctrBlock);
    }

    // check if there is a partial block at end.
    const uint8_t last = uint8_t(pInput + inputLength - xpos);
    if (last) {
        // encrypt into tmp and combine with last block of input
        ap->blockEncrypt(workspace->ctrBlock, pKey, workspace->tmp);
        for (uint8_t i = 0; i < last; i++)
            *ypos++ = *xpos++ ^ workspace->tmp[i];
    }
}
#endif
/**
 * @note    aes_gctr
 * @brief   performs gcntr operation for encryption
 * @param   pInput          pointer to input data (MUST BE be block multiple)
 * @param   inputLength     length of input array
 * @param   pKey            pointer to 128 bit AES key
 * @param   pICB            initial counter block J0
 * @param   pOutput         pointer to output data. length inputLength rounded up to 16.
 */
static void GCTRPadded(OTAES128E * const ap, GGBWS::GCTRPaddedWorkspace * const workspace,
                    const uint8_t *pInput, const uint8_t inputLength, const uint8_t *pKey,
                    const uint8_t *pCtrBlock, uint8_t *pOutput)
{
    const uint8_t *xpos = pInput;
    uint8_t *ypos = pOutput;

    // exit function if no input data
    if (inputLength == 0) return;

    // calculate number of full blocks to cipher
    const uint8_t n = inputLength / 16;

    // copy ICB to ctrBlock
    memcpy(workspace->ctrBlock, pCtrBlock, AES128GCM_BLOCK_SIZE);

    // for full blocks
    for (uint8_t i = 0; i < n; i++) {
        // cipher counterblock and combine with input
        ap->blockEncrypt(workspace->ctrBlock, pKey, ypos);
        xorBlock(ypos, xpos);

        // increment pointers to next block
        xpos += AES128GCM_BLOCK_SIZE;
        ypos += AES128GCM_BLOCK_SIZE;

        // increment counter
        incr32(workspace->ctrBlock);
    }

//    // check if there is a partial block at end.
//    const uint8_t last = uint8_t(pInput + inputLength - xpos);
//    if (last) {
//        // encrypt into tmp and combine with last block of input
//        ap->blockEncrypt(workspace->ctrBlock, pKey, workspace->tmp);
//        for (uint8_t i = 0; i < last; i++)
//            *ypos++ = *xpos++ ^ workspace->tmp[i];
//    }
}

/**
 * @note    ghash
 * @brief   performs authentication hashing
 * @todo    is final memcpy always persistent?
 * @param   pInput          pointer to input data
 * @param   inputLength     length of input array
 * @param   pAuthKey        pointer to 128 bit authentication subkey H
 * @param   pOutput         pointer to 16 byte output array
 */
static void GHASH(  GGBWS::GHASHWorkspace * const workspace,
                    const uint8_t *pInput, uint8_t inputLength,
                    const uint8_t *pAuthKey, uint8_t *pOutput )
{
    const uint8_t *xpos = pInput;
    // Calculate number of full blocks to hash.
    const uint8_t m = inputLength / AES128GCM_BLOCK_SIZE;

    // Hash full blocks.
    for (uint8_t i = 0; i < m; i++) {
        // Y_i = (Y^(i-1) XOR X_i) dot H
        xorBlock(pOutput, xpos);
        xpos += 16; // move to next block

        gFieldMultiply(workspace, pOutput, pAuthKey);

        // copy tmp to output
        memcpy(pOutput, workspace->ghashTmp, AES128GCM_BLOCK_SIZE);
    }

    // Check if final partial block.
    // Can be omitted if we use full blocks.
    if (pInput + inputLength > xpos) {
        // zero pad
        const uint8_t last = uint8_t(pInput + inputLength - xpos);
        memcpy(workspace->ghashTmp, xpos, last);
        memset(workspace->ghashTmp + last, 0, sizeof(workspace->ghashTmp) - last);

        // Y_i = (Y^(i-1) XOR X_i) dot H
        xorBlock(pOutput, workspace->ghashTmp);
        gFieldMultiply(workspace, pOutput, pAuthKey);
        memcpy(pOutput, workspace->ghashTmp, AES128GCM_BLOCK_SIZE);
    }
}

/**
 * @note    aes_gcm_prepare_j0
 * @brief   generates initial counter block from IV
 * @param   pIV             pointer to 12 byte initial vector nonce
 * @param   pOutput         pointer to 16 byte output array
 */
static void generateICB(const uint8_t *pIV, uint8_t *pOutput)
{
    // Prepare block J0 = IV || 0^31 || 1 [len(IV) = 96]
    memcpy(pOutput, pIV, AES128GCM_IV_SIZE);
    memset(pOutput + AES128GCM_IV_SIZE, 0, AES128GCM_BLOCK_SIZE - AES128GCM_IV_SIZE);
    pOutput[AES128GCM_BLOCK_SIZE - 1] = 0x01;
}

#if defined(OTAESGCM_ALLOW_UNPADDED)
/**
 * @note    aes_gcm_ctr
 * @brief   encrypt PDATA to get CDATA
 * @param   pICB        pointer to initial counter block
 * @param   pPDATA      pointer to plain text
 * @param   PDATALength length of plain text (need not be block-size multiple)
 * @param   pCDATA      pointer to array for cipher text. Length PDATALength rounded up to next 16 bytes
 */
static void generateCDATA(OTAES128E * const ap, GGBWS::GenCDATAWorkspace * const workspace,
                            const uint8_t *pICB, const uint8_t *pPDATA, uint8_t PDATALength,
                            uint8_t *pCDATA, const uint8_t *pKey )
{
    // Exit if no data to encrypt.
    if(PDATALength == 0) return;
    // Generate counter block J.
    memcpy(workspace->ctrBlock, pICB, AES128GCM_BLOCK_SIZE);
    incr32(workspace->ctrBlock);

    // Encrypt.
    GCTR(ap, &workspace->gctrSpace, pPDATA, PDATALength, pKey, workspace->ctrBlock, pCDATA);
}
#endif
/**
 * @note    aes_gcm_ctr
 * @brief   encrypt PDATA to get CDATA
 * @param   pICB        pointer to initial counter block
 * @param   pPDATA      pointer to plain text
 * @param   PDATALength length of plain text (MUST BE block-size multiple)
 * @param   pCDATA      pointer to array for cipher text. Length PDATALength rounded up to next 16 bytes
 */
static void generateCDATAPadded(OTAES128E * const ap, GGBWS::GenCDATAPaddedWorkspace * const cdataSpace,
                            const uint8_t *pICB, const uint8_t *pPDATAPadded, uint8_t PDATALength,
                            uint8_t *pCDATA, const uint8_t *pKey )
{
    // Exit if no data to encrypt.
    if(PDATALength == 0) return;

    // Generate counter block J.
    memcpy(cdataSpace->ctrBlock, pICB, AES128GCM_BLOCK_SIZE);
    incr32(cdataSpace->ctrBlock);

    // Encrypt.
    GCTRPadded(ap, &cdataSpace->gctrSpace, pPDATAPadded, PDATALength, pKey, cdataSpace->ctrBlock, pCDATA);
}

/**
 * @note    aes_gcm_ghash
 * @brief   makes message S from ADATA and CDATA
 * @param   pADATA          pointer to array containing authentication data
 * @param   ADATALength     length of ADATA array
 * @param   pCDATA          pointer to array containing encrypted data
 * @param   CDATALength     length of CDATA array
 * @param   pAuthKey        pointer to 128 bit authentication subkey H
 * @param   pTag            pointer to array to store tag
 */
static void generateTag(OTAES128E * const ap,
                            GGBWS::GenerateTagWorkspace * const workspace,
                            const uint8_t *pKey, const uint8_t *pAuthKey,
                            const uint8_t *pADATA, uint8_t ADATALength,
                            const uint8_t *pCDATA, uint8_t CDATALength,
                            uint8_t * pTag, const uint8_t *pICB)
{
    uint16_t temp;
    memset(workspace->lengthBuffer, 0, sizeof(workspace->lengthBuffer));
    memset(workspace->S, 0, sizeof(workspace->S));
    /*
     * u = 128 * ceil[len(C)/128] - len(C)
     * v = 128 * ceil[len(A)/128] - len(A)
     * S = GHASH_H(A || 0^v || C || 0^u || [len(A)]64 || [len(C)]64)
     * (i.e., zero padded to block size A || C and lengths of each in bits)
     */

    // function to put [len(A)]64 || [len(C)]64 in temp. could be saved as using fixed method length
    temp = (uint16_t) ADATALength * 8;
    //lengthBuffer[4] = (temp >> 24) & 0xff;    // these two are not needed as only using 16 bit values
    //lengthBuffer[5] = (temp >> 16) & 0xff;
    workspace->lengthBuffer[6] = (temp >> 8) & 0xff;
    workspace->lengthBuffer[7] = temp & 0xff;

    temp = (uint16_t) CDATALength * 8;
    //lengthBuffer[12] = (temp >> 24) & 0xff;
    //lengthBuffer[13] = (temp >> 16) & 0xff;
    workspace->lengthBuffer[14] = (temp >> 8) & 0xff;
    workspace->lengthBuffer[15] = temp & 0xff;

    GHASH(&workspace->ghashSpace, pADATA, ADATALength, pAuthKey, workspace->S);
    GHASH(&workspace->ghashSpace, pCDATA, CDATALength, pAuthKey, workspace->S);
    GHASH(&workspace->ghashSpace, workspace->lengthBuffer, sizeof(workspace->lengthBuffer), pAuthKey, workspace->S);

//    GCTR(ap, &workspace->gctrSpace, workspace->S, sizeof(workspace->S), pKey, pICB, pTag);
    GCTRPadded(ap, &workspace->gctrSpace, workspace->S, sizeof(workspace->S), pKey, pICB, pTag);
}

/**
 * @note    aes_gcm_init_hash_subkey
 * @brief   generates authentication subkey H
 * @param   pKey            pointer to 128 bit AES key
 * @param   pOutput         pointer to 16 byte array put to subkey H in
 * @note    tested arduino 1.6.5
 */
static void generateAuthKey(OTAES128E * const ap, const uint8_t *pKey, uint8_t *pAuthKey)
{
    // original has if(aes == NULL) return NULL;

    // Encrypt 128 bit block of 0s to generate authentication sub-key.
    memset(pAuthKey, 0, AES128GCM_BLOCK_SIZE);
    ap->blockEncrypt(pAuthKey, pKey, pAuthKey);
}


/******************* Public Functions ********************/
#if defined(OTAESGCM_ALLOW_UNPADDED)
/**
 * @brief   performs AES-GCM encryption.
 * @param   key             pointer to 16 byte (128 bit) key; never NULL
 * @param   IV              pointer to 12 byte (96 bit) IV; never NULL
 * @param   PDATA           pointer to plaintext array, this is internally padded up to a multiple of the blocksize; NULL if length 0.
 * @param   PDATALength     length of plaintext array in bytes, can be zero
 * @param   ADATA           pointer to additional data array; NULL if length 0.
 * @param   ADATALength     length of additional data in bytes, can be zero
 * @param   CDATA           buffer to output ciphertext to, size MUST BE PADDED/EXPANDED TO FULL BLOCKSIZE MULTIPLE at/above PDATAlength;
 *                          (nominally set to NULL if PDATA is NULL but seems to cause a crash)
 * @param   tag             pointer to 16 byte buffer to output tag to; never NULL
 * @retval  true if encryption successful, else false
 */
bool OTAES128GCMGenericBase::gcmEncrypt(
                        const uint8_t* key, const uint8_t* IV,
                        const uint8_t* PDATA, uint8_t PDATALength,
                        const uint8_t* ADATA, uint8_t ADATALength,
                        uint8_t* CDATA, uint8_t *tag)
{
    if(NULL == CDATA) { return(false); } // DHD20161107: NULL CDATA causes crashes in subroutines.

    // Check if there is input data.
    // Fail if there is nothing to encrypt and/or authenticate.
    if((PDATALength == 0) && (ADATALength == 0)) { return(false); }

    // Compute implicit CDATA length (ie rounded up to the next block size if necessary).
    if(PDATALength >= (uint8_t)(256U - (uint16_t)AES128GCM_BLOCK_SIZE)) { return(false); } // Too big.
    const uint8_t CDATALength = (PDATALength + AES128GCM_BLOCK_SIZE-1) & ~(AES128GCM_BLOCK_SIZE-1);

    GGBWS::GCMEncryptWorkspace workspace = getGCMEncryptWorkspace();

    // Encrypt data.
    generateAuthKey(ap, key, workspace.authKey);
    generateICB(IV, workspace.ICB);
    // ICB is hashed with the key then XORed with PDATA to encrypt plain text.
    generateCDATA(ap, &workspace.cdataWorkspace, workspace.ICB, PDATA, PDATALength, CDATA, key);

    // Generate authentication tag.
    generateTag(ap, &workspace.tagWorkspace, key, workspace.authKey, ADATA, ADATALength, CDATA, CDATALength, tag, workspace.ICB);

    // Erase workspace for security.
    memset(&workspace, 0, sizeof(workspace));

    return(true);
}
#endif
/**
 * @brief   performs AES-GCM encryption on padded data.
 *          If ADATA unused, set ADATA to NULL and ADATALength to 0.
 *          If PDATA unused (this is GMAC),
 *          then set PDATA and CDATA to NULL and PDATALength to 0.
 * @todo    Make GMAC helper function.
 * @param   key     pointer to 16 byte (128 bit) key; never NULL
 * @param   IV              pointer to 12 byte (96 bit) IV;
 *                          never NULL
 * @param   PDATA           pointer to plaintext input array,
 *                          MUST BT a multiple of the blocksize;
 *                          NULL if length 0.
 * @param   PDATALength     length of plaintext array in bytes,
 *                          can be zero,
 *                          MUST BE blocksize multiple.
 * @param   ADATA           pointer to additional input data array;
 *                          NULL if length 0.
 * @param   ADATALength     length of additional data in bytes,
 *                          can be zero
 * @param   CDATA           buffer to output ciphertext to,
 *                          size MUST BE PADDED/EXPANDED TO FULL
 *                          BLOCKSIZE MULTIPLE at/above PDATAlength;
 *                          (nominally set to NULL if PDATA is NULL
 *                          but seems to cause a crash)
 * @param   tag             pointer to 16 byte tag output buffer;
 *                          never NULL
 * @retval  true if encryption is successful, else false
 *
 * Plain-text must be an exact multiple of block length, eg padded.
 * This version may be smaller and faster and need less stack
 * if separately implemented, else default to generic gcmEncrypt().
 */
bool OTAES128GCMGenericBase::gcmEncryptPadded(
                        const uint8_t* key, const uint8_t* IV,
                        const uint8_t* PDATAPadded, uint8_t PDATALength,
                        const uint8_t* ADATA, uint8_t ADATALength,
                        uint8_t* CDATA, uint8_t *tag)
{
    if(NULL == CDATA) { return(false); } // DHD20161107: NULL CDATA causes crashes in subroutines.
    if(0 != (PDATALength & (AES128GCM_BLOCK_SIZE-1))) { return(false); } // Reject non-padded data.

    // Check if there is input data.
    // Fail if there is nothing to encrypt and/or authenticate.
    if((PDATALength == 0) && (ADATALength == 0)) { return(false); }

    const uint8_t CDATALength = PDATALength;

    GGBWS::GCMEncryptPaddedWorkspace &workspace = getGCMEncryptPaddedWorkspace();

    // Encrypt data.
    generateAuthKey(ap, key, workspace.authKey);
    generateICB(IV, workspace.ICB);
    // ICB is hashed with the key then XORed with PDATA to encrypt plain text.
    generateCDATAPadded(ap, &workspace.cdataWorkspace, workspace.ICB, PDATAPadded, PDATALength, CDATA, key);

    // Generate authentication tag.
    generateTag(ap, &workspace.tagWorkspace, key, workspace.authKey, ADATA, ADATALength, CDATA, CDATALength, tag, workspace.ICB);

    // Erase workspace for security.
    memset(&workspace, 0, sizeof(workspace));

    return(true);
}

/**
 * @brief   performs AES-GCM decryption and authentication
 * @param   key             pointer to 16 byte (128 bit) key
 * @param   IV              pointer to 12 byte (96 bit) IV
 * @param   CDATA           pointer to ciphertext array (multiple of block size, 16 bytes)
 * @param   CDATALength     length of ciphertext array
 * @param   ADATA           pointer to additional data array
 * @param   ADATALength     length of additional data
 * @param   PDATA           buffer to output plaintext to; must be same length as CDATA
 * @retval  true if decryption and authentication successful, else false
 */
bool OTAES128GCMGenericBase::gcmDecrypt(
                        const uint8_t* key, const uint8_t* IV,
                        const uint8_t* CDATA, uint8_t CDATALength,
                        const uint8_t* ADATA, uint8_t ADATALength,
                        const uint8_t* messageTag, uint8_t *PDATA)
{
    // Check if there is input data.
    // Fail if there is nothing to decrypt and/or authenticate.
    if((CDATALength == 0) && (ADATALength == 0)) { return(false); }

    // Fail if the CDATA length is not a multiple of the block size.
    if(0 != (CDATALength & (AES128GCM_BLOCK_SIZE-1))) { return(false); }
    GGBWS::GCMDecryptWorkspace &workspace = getGCMDecryptWorkspace();

    // Decrypt CDATA.
    generateAuthKey(ap, key, workspace.authKey);
    generateICB(IV, workspace.ICB);

    // ICB is hashed with the key then XORed with CDATA to decrypt cipher text.
    generateCDATAPadded(ap, &workspace.cdataWorkspace, workspace.ICB, CDATA, CDATALength, PDATA, key);

    // Authenticate and return true if tag matches.
    generateTag(ap, &workspace.tagWorkspace, key, workspace.authKey, ADATA, ADATALength, CDATA, CDATALength, workspace.calculatedTag, workspace.ICB);
    const bool success = (0 == checkTag(workspace.calculatedTag, messageTag));

    // Erase workspace for security.
    memset(&workspace, 0, sizeof(workspace));

    return(success);
}

#if defined(OTAESGCM_ALLOW_NON_WORKSPACE)
// AES-GCM 128-bit-key fixed-size text (256-bit/32-byte) encryption/authentication function.
// This is an adaptor/bridge function to ease outside use in simple cases
// without explicit type/library dependencies, but use with care.
// Stateless implementation: creates state on stack each time at cost of stack space
// (which may be considerable and difficult to manage in an embedded system)
// and at cost of time.
// The state parameter is not used (is ignored) and should be NULL.
// Other than the authtext, all sizes are fixed:
//   * textSize is 32 (or zero if plaintext is NULL)
//   * keySize is 16
//   * nonceSize is 12
//   * tagSize is 16
// The plain-text (and identical cipher-text) size is picked to be
// a multiple of the cipher's block size, or zero,
// which implies likely requirement for padding of the plain text.
// Note that the authenticated text size is not fixed, ie is zero or more bytes.
// The plaintext can be NULL, ciphertextOut not (a future version may allow it to be if plaintext is).
// Returns true on success, false on failure.
bool fixed32BTextSize12BNonce16BTagSimpleEnc_DEFAULT_STATELESS(void *const,
        const uint8_t *const key, const uint8_t *const iv,
        const uint8_t *const authtext, const uint8_t authtextSize,
        const uint8_t *const plaintext,
        uint8_t *const ciphertextOut, uint8_t *const tagOut)
    {
    if((NULL == key) || (NULL == iv) || (NULL == ciphertextOut) || (NULL == tagOut)) { return(false); } // ERROR
    OTAES128GCMGeneric<> i;
    return(i.gcmEncryptPadded(key, iv, plaintext, (NULL == plaintext) ? 0 : 32, (0 == authtextSize) ? NULL : authtext, authtextSize, ciphertextOut, tagOut));  // FIXME originally unpadded version
    }

// AES-GCM 128-bit-key fixed-size text (256-bit/32-byte) decryption/authentication function.
// This is an adaptor/bridge function to ease outside use in simple cases
// without explicit type/library dependencies, but use with care.
// Stateless implementation: creates state on stack each time at cost of stack space
// (which may be considerable and difficult to manage in an embedded system)
// and at cost of time.
// The state parameter is not used (is ignored) and should be NULL.
// Other than the authtext, all sizes are fixed:
//   * textSize is 32 (or zero if ciphertext is NULL)
//   * keySize is 16
//   * nonceSize is 12
//   * tagSize is 16
// The plain-text (and identical cipher-text) size is picked to be
// a multiple of the cipher's block size, or zero,
// which implies likely requirement for padding of the plain text.
// Note that the authenticated text size is not fixed, ie is zero or more bytes.
// The ciphertext can be NULL, plaintextOut not (a future version may allow it to be if ciphertext is).
// Decrypts/authenticates the output of fixed32BTextSize12BNonce16BTagSimpleEnc_DEFAULT_STATELESS.)
// Returns true on success, false on failure.
bool fixed32BTextSize12BNonce16BTagSimpleDec_DEFAULT_STATELESS(void *const,
        const uint8_t *const key, const uint8_t *const iv,
        const uint8_t *const authtext, const uint8_t authtextSize,
        const uint8_t *const ciphertext, const uint8_t *const tag,
        uint8_t *const plaintextOut)
    {
    if((NULL == key) || (NULL == iv) || (NULL == tag) || (NULL == plaintextOut)) { return(false); } // ERROR
    OTAES128GCMGeneric<> i;
    return(i.gcmDecrypt(key, iv, ciphertext, (NULL == ciphertext) ? 0 : 32, (0 == authtextSize) ? NULL : authtext, authtextSize, tag, plaintextOut));
    }
#endif

// AES-GCM 128-bit-key fixed-size text (256-bit/32-byte) encryption/authentication function using work space passed in.
// This is an adaptor/bridge function to ease outside use in simple cases
// without explicit type/library dependencies, but use with care.
// A workspace is passed in (and cleared on exit);
// this routine will fail (safely, returning false) if the workspace is NULL or too small.
// The workspace requirement depends on the implementation used.
// Other than the authtext, all sizes are fixed:
//   * textSize is 32 (or zero if plaintext is NULL)
//   * keySize is 16
//   * nonceSize is 12
//   * tagSize is 16
// The plain-text (and identical cipher-text) size is picked to be
// a multiple of the cipher's block size, or zero,
// which implies likely requirement for padding of the plain text.
// Note that the authenticated text size is not fixed, ie is zero or more bytes.
// Returns true on success, false on failure.
bool fixed32BTextSize12BNonce16BTagSimpleEnc_DEFAULT_WITH_LWORKSPACE(
        uint8_t *const workspace, const size_t workspaceSize,
        const uint8_t *const key, const uint8_t *const iv,
        const uint8_t *const authtext, const uint8_t authtextSize,
        const uint8_t *const plaintext,
        uint8_t *const ciphertextOut, uint8_t *const tagOut)
    {
    if((NULL == key) || (NULL == iv) || (NULL == ciphertextOut) || (NULL == tagOut)) { return(false); } // ERROR
    typedef OTAES128GCMGenericWithWorkspace<> t;
    if(!t::isWorkspaceSufficientEncPadded(workspace, workspaceSize))
        {
#if 1 && !defined(ARDUINO_ARCH_AVR)
// V0P2BASE_DEBUG_SERIAL_PRINTLN_FLASHSTRING(fs) { OTV0P2BASE::serialPrintlnAndFlush(F(fs)); }
        fprintf(stderr, "ERROR: insufficient workspace to encrypt: %lu vs %lu\n", workspaceSize, t::workspaceRequiredEncPadded);
#endif
        return(false); // ERROR
        }
#if 1 && !defined(ARDUINO_ARCH_AVR)
    else if(workspaceSize > t::workspaceRequiredMax)
        {
        fprintf(stderr, "WARNING: clear excess workspace to encrypt: %lu vs %lu\n", workspaceSize, t::workspaceRequiredEncPadded);
        }
#endif
    t i(workspace, workspaceSize);
#if !defined(OTAESGCM_ALLOW_UNPADDED)
    return(i.gcmEncryptPadded(key, iv, plaintext, (NULL == plaintext) ? 0 : 32, (0 == authtextSize) ? NULL : authtext, authtextSize, ciphertextOut, tagOut));
#else
    return(i.gcmEncrypt(key, iv, plaintext, (NULL == plaintext) ? 0 : 32, (0 == authtextSize) ? NULL : authtext, authtextSize, ciphertextOut, tagOut));
#endif
    }

// AES-GCM 128-bit-key fixed-size text (256-bit/32-byte) decryption/authentication function using work space passed in.
// This is an adaptor/bridge function to ease outside use in simple cases
// without explicit type/library dependencies, but use with care.
// A workspace is passed in (and cleared on exit);
// this routine will fail (safely, returning false) if the workspace is NULL or too small.
// The workspace requirement depends on the implementation used.
// Other than the authtext, all sizes are fixed:
//   * textSize is 32 (or zero if ciphertext is NULL)
//   * keySize is 16
//   * nonceSize is 12
//   * tagSize is 16
// The plain-text (and identical cipher-text) size is picked to be
// a multiple of the cipher's block size, or zero,
// which implies likely requirement for padding of the plain text.
// Note that the authenticated text size is not fixed, ie is zero or more bytes.
// Decrypts/authenticates the output of fixed32BTextSize12BNonce16BTagSimpleEnc_DEFAULT_STATELESS.)
// Returns true on success, false on failure.
bool fixed32BTextSize12BNonce16BTagSimpleDec_DEFAULT_WITH_LWORKSPACE(
        uint8_t *const workspace, const size_t workspaceSize,
        const uint8_t *const key, const uint8_t *const iv,
        const uint8_t *const authtext, const uint8_t authtextSize,
        const uint8_t *const ciphertext, const uint8_t *const tag,
        uint8_t *const plaintextOut)
    {
    if((NULL == key) || (NULL == iv) || (NULL == tag) || (NULL == plaintextOut)) { return(false); } // ERROR
    typedef OTAES128GCMGenericWithWorkspace<> t;
    if(!t::isWorkspaceSufficientDec(workspace, workspaceSize))
        {
#if 1 && !defined(ARDUINO_ARCH_AVR)
        fprintf(stderr, "ERROR: insufficient workspace to decrypt: %lu vs %lu\n", workspaceSize, t::workspaceRequiredDec);
#endif
        return(false); // ERROR
        }
#if 1 && !defined(ARDUINO_ARCH_AVR)
    else if(workspaceSize > t::workspaceRequiredMax)
        {
        fprintf(stderr, "WARNING: clear excess workspace to decrypt: %lu vs %lu\n", workspaceSize, t::workspaceRequiredDec);
        }
#endif

    t i(workspace, workspaceSize);
    return(i.gcmDecrypt(key, iv, ciphertext, (NULL == ciphertext) ? 0 : 32, (0 == authtextSize) ? NULL : authtext, authtextSize, tag, plaintextOut));
    }



    }
