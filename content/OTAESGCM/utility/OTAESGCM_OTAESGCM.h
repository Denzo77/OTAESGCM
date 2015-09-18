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

Author(s) / Copyright (s): Deniz Erbillgin 2015
                           Damon Hart-Davis 2015
*/

/* OpenTRV OTAESGCM microcontroller-/IoT- friendly AES(128)-GCM implementation. */

#ifndef ARDUINO_LIB_OTAESGCM_OTAESGCM_H
#define ARDUINO_LIB_OTAESGCM_OTAESGCM_H

#include <stddef.h>
#include <stdint.h>


// Use namespaces to help avoid collisions.
namespace OTAESGCM
    {


static const uint8_t GCM_BLOCK_SIZE = 16; // GCM block size in bytes. This must be the same as the AES block size.
static const uint8_t GCM_IV_SIZE    = 12; // GCM initialisation size in bytes.
static const uint8_t GCM_TAG_SIZE   = 16; // GCM authentication tag size in bytes.

/**
 * @note
 * @brief    performs aesgcm encryption
 * @todo     should this be a void?
 * @param    key                pointer to 16 byte (128 bit) key
 * @param    IV                pointer to IV
 * @param    PDATA            pointer to plaintext array
 * @param    PDATA_length    length of plaintext array
 * @param    ADATA             pointer to additional data array
 * @param    ADATA_length    length of additional data
 * @param    CDATA            buffer to output ciphertext to
 * @param    tag                pointer to 16 byte buffer to output tag to
 */
bool aes128_gcm_encrypt(    const uint8_t* key, const uint8_t* IV,
                            const uint8_t* PDATA, uint8_t PDATALength,
                            uint8_t* ADATA, uint8_t ADATALength,
                            uint8_t* CDATA, uint8_t *tag);

/**
 * @note
 * @brief   performs aesgcm decryption and authentication
 * @todo    How to do data hiding on authentication fail?
 *                 - when put into classes?
 *                 - wipe array?
 *                 - make PDATA private and then only pass pointer if true?
 *             Should this be a bool?
 * @param    key:            pointer to 16 byte (128 bit) key
 * @param    IV:                pointer to IV
 * @param    CDATA:          pointer to ciphertext array
 * @param    CDATALength:   length of ciphertext array
 * @param    ADATA:          pointer to additional data array
 * @param    ADATALength:   length of additional data
 * @param    PDATA:          buffer to output plaintext to
 * @retval    returns true if authenticated, else returns false
 */
uint8_t aes128_gcm_decrypt( const uint8_t* key, const uint8_t* IV,
                            const uint8_t* CDATA, uint8_t CDATALength,
                            const uint8_t* ADATA, uint8_t ADATALength,
                            const uint8_t* messageTag, uint8_t *PDATA);


    }







#endif
