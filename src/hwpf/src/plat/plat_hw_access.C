/* IBM_PROLOG_BEGIN_TAG                                                   */
/* This is an automatically generated prolog.                             */
/*                                                                        */
/* $Source: src/hwpf/src/plat/plat_hw_access.C $                          */
/*                                                                        */
/* OpenPOWER sbe Project                                                  */
/*                                                                        */
/* Contributors Listed Below - COPYRIGHT 2016,2017                        */
/* [+] International Business Machines Corp.                              */
/*                                                                        */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/*                                                                        */
/* IBM_PROLOG_END_TAG                                                     */

#include <fapi2.H>
#include "hw_access.H"
#include "plat_hw_access.H"
#include "p9_perv_scom_addresses.H"
#include <p9_putRingUtils.H>

namespace fapi2
{

struct restoreOpcgRegisters g_opcgData;

uint64_t decodeScanRegionData(const uint32_t i_ringAddress)
{
    uint32_t l_scan_region = (i_ringAddress & 0x0000FFF0) << 13;

    uint32_t l_scan_type = 0x00008000 >> (i_ringAddress & 0x0000000F);

    // This is special case if encoded type is 0xF

    if ( (i_ringAddress & 0x0000000F) == 0xF)
    {
        l_scan_type = 0x00008000 | (l_scan_type << 12);
    }
    uint64_t l_value = l_scan_region;
    l_value = (l_value << 32) |  l_scan_type;

    return l_value;
}

ReturnCode getRing_setup(const uint32_t i_ringAddress,
                             const RingMode i_ringMode)
{
    fapi2::ReturnCode l_rc = FAPI2_RC_SUCCESS;
    uint64_t l_scanRegion = 0;
    uint32_t l_chipletId = i_ringAddress >> 24;

    Target<fapi2::TARGET_TYPE_PROC_CHIP> l_proc = plat_getChipTarget();

    do
    {
        l_scanRegion = decodeScanRegionData(i_ringAddress);

        if (i_ringMode &  fapi2::RING_MODE_SET_PULSE_SL)
        {
            l_rc =  storeOPCGRegData (l_proc, g_opcgData, l_chipletId);
            if(l_rc != fapi2::FAPI2_RC_SUCCESS)
            {
                break;
            }

            l_rc = setupScanRegionForSetPulse(l_proc, l_scanRegion,
                                        i_ringMode,l_chipletId);
            if(l_rc != fapi2::FAPI2_RC_SUCCESS)
            {
                break;
            }
        }
        else
        {
            // Set up the scan region for the ring.
            l_rc = setupScanRegion(l_proc, l_scanRegion, l_chipletId);
            if(l_rc != fapi2::FAPI2_RC_SUCCESS)
            {
                break;
            }
        }
        // Write a 64 bit value for header.
        const uint64_t l_header = 0xa5a5a5a5a5a5a5a5;
        uint32_t l_scomAddress = 0x0003E000 |  (i_ringAddress & 0xFF000000);
        l_rc = fapi2::putScom(l_proc, l_scomAddress, l_header);
        if(l_rc != fapi2::FAPI2_RC_SUCCESS)
        {
            break;
        }

    }while(0);

    return l_rc;
}

ReturnCode getRing_granule_data(const uint32_t i_ringAddress,
                                uint64_t *o_data,
                                const uint32_t i_bitShiftValue)
{
    fapi2::ReturnCode l_rc = FAPI2_RC_SUCCESS;
    uint32_t l_chipletId = i_ringAddress & 0xFF000000;

    fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> l_proc;

    uint32_t l_scomAddress = 0x00038000;
    l_scomAddress |= l_chipletId;

    do
    {
        fapi2::buffer<uint64_t> l_ringData;
        l_rc = fapi2::getScom(l_proc, l_scomAddress, l_ringData);
        if(l_rc != fapi2::FAPI2_RC_SUCCESS)
        {
            break;
        }
        *o_data = l_ringData;

        l_scomAddress = 0x00038000 | i_bitShiftValue;
        l_scomAddress |= l_chipletId;

        l_rc = fapi2::getScom(l_proc, l_scomAddress, l_ringData);
        if(l_rc != fapi2::FAPI2_RC_SUCCESS)
        {
            break;
        }
    } while(0);

    return l_rc;
}


ReturnCode getRing_verifyAndcleanup(const uint32_t i_ringAddress,
                                    const RingMode i_ringMode)
{
    fapi2::ReturnCode l_rc = FAPI2_RC_SUCCESS;
    uint32_t l_chipletId = i_ringAddress >> 24;

    fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> l_proc;

    do
    {
        // Verify header
        uint64_t l_header = 0xa5a5a5a5a5a5a5a5;
        l_rc = verifyHeader(l_proc, l_header, l_chipletId,i_ringMode);
        if(l_rc != fapi2::FAPI2_RC_SUCCESS)
        {
            break;
        }

        l_rc = cleanScanRegionandTypeData(l_proc, l_chipletId);
        if(l_rc != fapi2::FAPI2_RC_SUCCESS)
        {
            break;
        }

        if (i_ringMode &  fapi2::RING_MODE_SET_PULSE_SL)
        {
            l_rc =  restoreOPCGRegData(l_proc, g_opcgData, l_chipletId);
            if(l_rc != fapi2::FAPI2_RC_SUCCESS)
            {
                break;
            }
        }

    }while(0);

    return l_rc;
}

static uint32_t getEffectiveAddress(const plat_target_handle_t &i_target, const uint32_t i_addr)
{
    ScomAddr l_addr(i_addr);
    switch(i_target.getTargetType())
    {
        case PPE_TARGET_TYPE_EX:
            if((EQ_CHIPLET_OFFSET <= l_addr.iv_chiplet) &&
               ((EQ_CHIPLET_OFFSET + EQ_TARGET_COUNT) > l_addr.iv_chiplet))
            {
                l_addr.iv_chiplet = i_target.fields.chiplet_num;
                l_addr.iv_ring = (l_addr.iv_ring - (l_addr.iv_ring % 2)) +
                                  (i_target.getTargetInstance() % 2);
            }
            else if ((CORE_CHIPLET_OFFSET <= l_addr.iv_chiplet) &&
                     ((CORE_CHIPLET_OFFSET + CORE_TARGET_COUNT) > l_addr.iv_chiplet))
            {
                l_addr.iv_chiplet = CORE_CHIPLET_OFFSET + (l_addr.iv_chiplet % 2) +
                                    (i_target.getTargetInstance() * 2);
            }
            else
            {
               assert(false);
            }
            break;
        case PPE_TARGET_TYPE_MCS:
            l_addr.iv_chiplet = i_target.fields.chiplet_num;
            l_addr.iv_satId = (2 * (i_target.getTargetInstance() % 2));
            break;
        default:
            if(0 != i_target.getAddressOverlay())
            {
                l_addr.iv_chiplet = i_target.fields.chiplet_num;
            }
            break;
    }
    return l_addr;
}

fapi2::ReturnCode getscom_abs_wrap(const void *i_target,
                                   const uint32_t i_addr, uint64_t *o_data)
{
    uint32_t l_pibRc = 0;
    uint32_t l_addr = getEffectiveAddress(*(plat_target_handle_t*)i_target, i_addr);
    FAPI_INF("getScom: address: 0x%08X", l_addr);
    l_pibRc = getscom_abs(l_addr, o_data);
    if( PIB_NO_ERROR != l_pibRc )
    {
        l_pibRc = p9_pibErrRetry( l_addr, o_data, l_pibRc, true);
    }
    FAPI_INF("getScom: returned pibRc: 0x%08X, data HI: 0x%08X, "
             "data LO: 0x%08X", l_pibRc, (*o_data >> 32),
             static_cast<uint32_t>(*o_data & 0xFFFFFFFF));
    fapi2::current_err = fapi2::FAPI2_RC_SUCCESS;
    // Setting 64bit address in ffdc package as the parsers are
    // hard coded to read 64bit address
    const uint64_t ffdcAddr = l_addr;
    PLAT_FAPI_ASSERT( PIB_NO_ERROR == l_pibRc,
                      SBE_SCOM_FAILURE().
                      set_address(ffdcAddr).
                      set_pcb_pib_rc(l_pibRc),
                      "getScom:pcb pib error, pibRc[0x%08X] Scom_Addr[0x%08X]",
                      l_pibRc,l_addr);
fapi_try_exit:
    return fapi2::current_err;
}

fapi2::ReturnCode putscom_abs_wrap(const void *i_target,
                                    const uint32_t i_addr, uint64_t i_data)
{
    uint32_t l_pibRc = 0;
    uint32_t l_addr = getEffectiveAddress(*(plat_target_handle_t*)i_target,
                                                                    i_addr);
    FAPI_INF("putScom: address: 0x%08X, data HI: 0x%08X, data LO: 0x%08X",
             l_addr, (i_data >> 32),
             static_cast<uint32_t>(i_data & 0xFFFFFFFF));
    l_pibRc = putscom_abs(l_addr, i_data);
    if( PIB_NO_ERROR != l_pibRc )
    {
        l_pibRc = p9_pibErrRetry( l_addr, &i_data, l_pibRc, false);
    }
    FAPI_INF("putScom: returned pibRc: 0x%08X", l_pibRc);
    fapi2::current_err = fapi2::FAPI2_RC_SUCCESS;
    // Setting 64bit address in ffdc package as the parsers are
    // hard coded to read 64bit address
    const uint64_t ffdcAddr = l_addr;
    PLAT_FAPI_ASSERT( PIB_NO_ERROR == l_pibRc,
                      SBE_SCOM_FAILURE().
                      set_address(ffdcAddr).
                      set_pcb_pib_rc(l_pibRc),
                      "putScom:pcb pib error, pibRc[0x%08X] Scom_Addr[0x%08X] "
                      "Scom_Data[0x%08X%08X]",
                      l_pibRc,l_addr,(i_data >> 32),
                      static_cast<uint32_t>(i_data & 0xFFFFFFFF));
fapi_try_exit:
    return fapi2::current_err;
}

uint32_t p9_pibErrRetry( const uint32_t i_addr, uint64_t *io_data,
                         const uint8_t i_pibErr, const bool i_isRead)
{
    FAPI_INF("Entering p9_pibErrRetry");
    static const uint8_t MAX_RETRIES = 2;
    static const uint64_t REG_BIT0 = (uint64_t)(0x1)<<63;
    uint8_t l_retryCount = 0;
    uint32_t pibErr = i_pibErr;

    do
    {
        // Only retry for parity and timeout errors
        if (( i_pibErr != PIB_PARITY_ERROR )
            && ( i_pibErr !=  PIB_TIMEOUT_ERROR ))
        {
            break;
        }
        for(l_retryCount = 0; l_retryCount < MAX_RETRIES; l_retryCount++)
        {
            // RESET_PCB: Reset all PCB elements outside of the standby domain
            pibErr = putscom_abs( PERV_PIB_RESET_REG, REG_BIT0 );
            if( pibErr ) break;
            // RESET_PCB: clear it again
            pibErr = putscom_abs( PERV_PIB_RESET_REG, 0);
            if( pibErr ) break;

            FAPI_DBG( "*** Retry %i ***", l_retryCount );

            if ( i_isRead )
            {
                pibErr = getscom_abs(i_addr, io_data);
            }
            else
            {
                pibErr = putscom_abs(i_addr, *io_data);
            }

            if( PIB_NO_ERROR == pibErr )
            {
                FAPI_INF("Read/Write Retry Successful");
                break;
            }
            if ( pibErr != i_pibErr ) break;
        }
    }while(0);
    FAPI_INF("Exiting p9_pibErrRetry");
    return pibErr;
}

};
