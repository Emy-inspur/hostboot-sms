/* IBM_PROLOG_BEGIN_TAG                                                   */
/* This is an automatically generated prolog.                             */
/*                                                                        */
/* $Source: src/include/usr/fapi2/subroutine_executor.H $                 */
/*                                                                        */
/* OpenPOWER HostBoot Project                                             */
/*                                                                        */
/* Contributors Listed Below - COPYRIGHT 2016,2020                        */
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
///
/// @file subroutine_executor.H
///
/// @brief Defines the PLAT Subroutine Executor Macro.
///
/// The PLAT Subroutine Executor macro is called by
/// FAPI_CALL_SUBROUTINE when a hardware procedure has
/// a subroutine is needed, typicaly a chipop function.
///
/// Example implementation of plat code

#ifndef SUBROUTINEEXECUTOR_H_
#define SUBROUTINEEXECUTOR_H_

#include <fapi2_subroutine_executor.H>
#include <errl/errlmanager.H>
#include <string.h>
#include <stdarg.h>
#include <sbeio/sbeioif.H>
#include <plat_trace.H>
#include <secureboot/service.H>
#include <plat_hw_access.H>

// Debug trace
#define  DEBUG_TRACE  0   // 0 = disable
// Defining these two symbolically simplifies the include chain
#define  PCI0_PERV_CHIPLET_ID 0x08
#define  PCI1_PERV_CHIPLET_ID 0x09

/**
* @brief Given a hwp name, and its parameters, serialize the parameters and pass the serialized
*        data to the SBE via a FIFO chipop
* @param[in]  i_hwpName  String representing the name of the hwp to be called
* @param[in]  types      Any variable length of arguments, which along with target should get passed
*                        in as parameters to the given HWP
* @return errlHndl_t Error log handle on failure.
*/
template<class... Types>
errlHndl_t requestHwpViaSbe(const char * i_hwpName, Types... types)
{
    errlHndl_t l_errl = nullptr;

    // Retrieve argument byte size
    size_t l_sizeOfArgInBytes =0;
    using expander = int[];
    (void) expander{ 0, (l_sizeOfArgInBytes+=sizeof(types), 0)... };

    // Retrieve the remaining arguments
    uint8_t l_buffer[l_sizeOfArgInBytes];
    uint8_t* l_bufferPtr = &(l_buffer[0]);
    memset(l_bufferPtr, 0, l_sizeOfArgInBytes);
    (void) expander{ 0, ((memcpy(l_bufferPtr,&types,sizeof(types)),l_bufferPtr+=sizeof(types)), 0)... };

    FAPI_INF("requestHwpViaSbe - HWP name %s, Size of arguments %d bytes",
            i_hwpName, l_sizeOfArgInBytes);

#if DEBUG_TRACE
    for (uint32_t ii = 0; ii < l_sizeOfArgInBytes; ii++)
    {
         FAPI_INF("requestHwpViaSbe - l_buffer[%d] = 0x%.2X", ii, l_buffer[ii]);
    }
#endif

    //Call the chip op send function to request the SBE to call the HWP
    l_bufferPtr = &(l_buffer[0]);
    l_errl = SBEIO::sendSecureHwpRequest(l_bufferPtr,
                  l_sizeOfArgInBytes, i_hwpName);

    return l_errl;
}

// Macros that return the 1st argument
#define _GET_1ST_ARG(N, ...) N

// Macros that return the 2nd argument
#define _GET_2ND_ARG(N, M, ...) M

/** @brief Helper struct to force evaluation of constexpr expressions at
 *         compile-time.
 */
template<typename T, T x, T y>
struct values_equal
{
    static constexpr bool value = x == y;
};

// The list of HWP names to execute on the HOST, as opposed to delegating them
// to be run on the SBE.
static constexpr const char* const hwp_whitelist[] =
{
  "p10_putmemproc"
};

/** @brief strcmp that can run at compile-time given constexpr arguments.
 */
static constexpr int compile_time_strcmp(const char* a, const char* b)
{
    while (*a && *a == *b)
    {
        ++a;
        ++b;
    }

    return *a - *b;
}

/** @brief Determines whether the given HWP name should be run on the host.
 *
 * @param[in] name      Name of hardware procedure (not including arguemnts)
 * @return              True if the HWP should be executed on the host, false otherwise
 */
static constexpr bool is_hwp_whitelisted(const char* const name)
{
    bool is_whitelisted = false;

    for (size_t i = 0; i < std::size(hwp_whitelist); ++i) {
        if (compile_time_strcmp(name, hwp_whitelist[i]) == 0)
        {
            is_whitelisted = true;
            break;
        }
    }

    return is_whitelisted;
}

/**
 * @brief Subroutine Executor macro example code - Platforms will need to
 *        implement as needed for their enviroment.
 *
 * This macro calls a PLAT macro which will do any platform specific work to
 * execute the Subroutine (e.g. dlopening a shared library)
 */
#define FAPI_PLAT_CALL_SUBROUTINE(RC, FUNC, _args...) \
{ \
    do \
    { \
        errlHndl_t l_errl = nullptr; \
        /* Read the FUNC as a string and pass it to the conversion method \
        to determine what hwp we need to request */ \
        const char* l_function = #FUNC; \
        do  \
        { \
            if (values_equal<bool, is_hwp_whitelisted(#FUNC), true>::value) \
            { \
                FAPI_INF("subroutine_executor: executing hwp %s natively", \
                         l_function); \
                FAPI_PLAT_EXEC_HWP(RC, FUNC, _args); \
            } \
            else \
            { \
                FAPI_INF("subroutine_executor: executing hwp %s via SBE chip-op", \
                          l_function); \
                /* Check if the target exists on the boot proc*/ \
                bool isBootProc = true; \
                /* Read the target argument, which is the first argument in the list */ \
                TARGETING::Target* l_target = \
                    _GET_1ST_ARG(_args).get(); \
                l_errl = fapi2::isOnBootProc(l_target, isBootProc); \
                if (l_errl) \
                { \
                    FAPI_INF("subroutine_executor: Failed trying to determine if target was on boot proc chip"); \
                    addErrlPtrToReturnCode(RC, l_errl); \
                    l_errl = nullptr; \
                    break; \
                } \
                /* Run hwp on host if targ is on boot proc or if PCI chiplet*/ \
                if (strcmp(l_function, "p10_putsram") == 0 && \
                        (isBootProc || \
                         _GET_2ND_ARG(_args) == PCI0_PERV_CHIPLET_ID || \
                         _GET_2ND_ARG(_args) == PCI1_PERV_CHIPLET_ID)) \
                { \
                    FAPI_INF("subroutine_executor:    isBootProc = %d .. executing hwp %s on host", \
                            isBootProc, l_function); \
                    FAPI_PLAT_EXEC_HWP(RC, FUNC, _args); \
                } \
                else \
                { \
                    l_errl =  requestHwpViaSbe(l_function, _args); \
                } \
            } \
            break; \
        } while (0); \
        if(l_errl) \
        { \
            FAPI_INF("subroutine_executor: SBE HWP invoking returns error. " \
            "Commit error"); \
            /*Commit the error as unrecoverable */ \
            l_errl->setSev(ERRORLOG::ERRL_SEV_UNRECOVERABLE); \
            errlCommit(l_errl, SBEIO_COMP_ID); \
        } \
        else \
        { \
            FAPI_INF("subroutine_executor: SBE HWP invoking successful."); \
        } \
    } while(0); \
}

#endif
