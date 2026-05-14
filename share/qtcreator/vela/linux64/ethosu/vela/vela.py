#!/usr/bin/env python3
# flake8: noqa
#
# SPDX-FileCopyrightText: Copyright 2020-2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the License); you may
# not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an AS IS BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
"""Compile a neural network model for Arm Ethos-U NPUs."""
import argparse
import glob
import mmap
import os
import sys
import time
from configparser import ConfigParser
from typing import List
from typing import Optional

import flatbuffers

from . import architecture_features
from . import compiler_driver
from . import model_reader
from . import rawdata_writer
from . import scheduler
from . import stats_writer
from . import tflite_writer
from ._version import __version__
from .api import API_VERSION
from .debug_database import DebugDatabase
from .errors import InputFileError
from .errors import VelaError
from .hillclimb_allocation import HillClimbAllocator
from .nn_graph import TensorAllocator
from .tensor import MemArea
from .tensor import Tensor
from .tflite.Model import Model
from ethosu import regor

TFLITE_MAGIC = 0x334C4654
TOSA_MAGIC = 0x41534F54


def process(
    input_name, enable_debug_db, arch, model_reader_options, compiler_options, scheduler_options, output_format
):
    if compiler_options.timing:
        start = time.time()

    os.makedirs(compiler_options.output_dir, exist_ok=True)
    output_basename = os.path.join(compiler_options.output_dir, os.path.splitext(os.path.basename(input_name))[0])
    DebugDatabase.show_warnings = enable_debug_db

    nng, network_type = model_reader.read_model(input_name, model_reader_options)

    if not nng:
        raise InputFileError(input_name, "Input file could not be read")

    if compiler_options.verbose_operators:
        nng.print_operators()

    if compiler_options.timing:
        stop = time.time()
        print("Model reading took %f s" % (stop - start))
        start = time.time()

    compiler_driver.compiler_driver(nng, arch, compiler_options, scheduler_options, network_type, output_basename)

    summary_csv_file = "{0}_summary_{1}.csv".format(output_basename, arch.system_config)
    stats_writer.write_summary_metrics_csv(nng, summary_csv_file, arch)

    stats_writer.print_performance_metrics(
        nng,
        show_cpu_operations=compiler_options.show_cpu_operations,
        verbose_weights=compiler_options.verbose_weights,
        verbose_cycle_estimate=compiler_options.verbose_cycle_estimate,
        arch=arch,
    )

    output_tfl_filename = output_basename + "_vela.tflite"
    if output_format == "tflite":
        tflite_writer.write_tflite(nng, output_tfl_filename)
    elif output_format == "raw":
        rawdata_writer.write_rawdata_output(nng, arch, output_basename)
    else:
        assert False, f"Unsupported output_format = {output_format}"

    if enable_debug_db:
        file_offsets = calculate_operator_file_offsets(output_tfl_filename)
        for idx, offset in enumerate(sorted(file_offsets)):
            sg = find_subgraph_with_command_stream_order(nng, idx)
            if sg is not None:
                DebugDatabase.set_stream_offset(sg, offset)
        debug_filename = output_basename + "_debug.xml"
        DebugDatabase.write(debug_filename, input_name, output_tfl_filename)

    if compiler_options.timing:
        stop = time.time()
        print("Compiler driver took %f s" % (stop - start))

    return nng


def process_regor(
    arch,
    input_name,
    accelerator,
    system_config,
    options,
    enable_debug_db,
    output_dir,
    output_format,
    verbose_weights=False,
    verbose_cycle_estimate=False,
    show_cpu_operations=False,
    verbose_performance=False,
):
    os.makedirs(output_dir, exist_ok=True)

    with open(input_name, "rb") as f:
        with mmap.mmap(f.fileno(), length=0, access=mmap.ACCESS_READ) as network:
            fmt = get_format(network)
            compiled_model = regor.compile(accelerator, network, fmt, system_config, options=options, verbose=True)

    model_name = os.path.splitext(os.path.basename(input_name))[0]

    output_basename = os.path.join(output_dir, model_name)

    if output_format == "tflite":
        assert isinstance(compiled_model, regor.CompiledTFLiteModel)
        output_name = output_basename + "_vela.tflite"
        with open(output_name, "wb") as f:
            f.write(compiled_model.model)
    elif output_format == "raw":
        assert isinstance(compiled_model, regor.CompiledRawModel)
        rawdata_writer.write_rawdata_output_from_model(output_basename, compiled_model)
    else:
        assert False, f"Unsupported output_format = {output_format}"

    summary_csv_file = "{0}_summary_{1}.csv".format(output_basename, arch.system_config)

    if verbose_performance:
        stats_writer.write_regor_perlayer_performance_csv(
            arch, compiled_model.opt_database, compiled_model.perf_report, output_basename
        )
        stats_writer.print_regor_perlayer_performance(
            arch, compiled_model.opt_database, compiled_model.perf_report, output_basename
        )

    stats_writer.print_regor_performance_metrics(
        arch,
        compiled_model.perf_report,
        model_name,
        summary_csv_file,
        compiled_model.opt_database,
        verbose_weights,
        verbose_cycle_estimate,
        show_cpu_operations,
    )
    if enable_debug_db:
        stats_writer.write_regor_db(compiled_model.opt_database, output_basename)


def find_subgraph_with_command_stream_order(nng, idx):
    for sg in nng.subgraphs:
        if sg.generated_stream_id == idx:
            return sg
    return None


def calculate_operator_file_offsets(name: str):
    # Read the vela optimized TFLite file
    with open(name, "rb") as f:
        buf = bytearray(f.read())
    # Calculate the file offsets for each custom operator
    file_offsets = []
    model = Model.GetRootAsModel(buf, 0)
    for idx in range(model.SubgraphsLength()):  # However only one subgraph is supported as of now
        sg = model.Subgraphs(idx)
        for idx in range(sg.OperatorsLength()):
            operator = sg.Operators(idx)
            if model.OperatorCodes(operator.OpcodeIndex()).CustomCode() is not None:
                tensor_idx = operator.Inputs(0)
                tensor = sg.Tensors(tensor_idx)
                buffer = model.Buffers(tensor.Buffer())
                offset = flatbuffers.number_types.UOffsetTFlags.py_type(buffer._tab.Offset(4))
                file_offsets.append(buffer._tab.Vector(offset))
    return file_offsets


def print_subgraph_io_summary(nng):
    """Print a summary of all the input and output tensor sizes for all subgraphs.
    Also displays the total tensor size and the memory used area for sram.
    """

    print("Subgraph IO Summary")
    print("-------------------")
    print(f"NNG: {nng.name}")
    max_sg_size = 0
    for sg in reversed(nng.subgraphs):
        print(f"  NNG Subgraph: {sg.name} = {sg.placement}")
        sg_size = 0

        if hasattr(sg, "scratch_tensor") and sg.scratch_tensor is not None:
            sg_tensors = sg.input_tensors + [sg.scratch_tensor] + sg.output_tensors
        else:
            sg_tensors = sg.input_tensors + sg.output_tensors

        for tens in sg_tensors:
            if tens in sg.input_tensors:
                tens_dir = "In"
            elif tens in sg.output_tensors:
                tens_dir = "Out"
            else:
                tens_dir = "In/Out"

            size = tens.elements() * tens.element_size() / 1024.0
            sg_size = sg_size + size
            print(f"         Tensor [{tens_dir}]: {tens.name} = {size} KiB")

        print(f"      Total Size = {sg_size} KiB")
        print(f"      SRAM Memory Used = {sg.memory_used.get(MemArea.Sram, 0) / 1024.0} KiB")
        max_sg_size = max(sg_size, max_sg_size)

    print(f"   Maximum NNG Subgraph Size = {max_sg_size} KiB")


def generate_supported_ops():
    def _u85_tosa_support_lines() -> list[str]:
        return [
            "## Ethos-U85 TOSA Operator Support",
            "",
            "This section summarizes TOSA support for Ethos-U85 against the v1.0 PRO-INT 8k baseline.",
            "",
            "- Baseline:",
            "  - TOSA Version: v1.0",
            "  - TOSA Profile: PRO-INT",
            "  - TOSA Level: 8k",
            "- Supported extensions:",
            "  - EXT-INT16",
            "  - EXT-INT4",
            "  - EXT-DOUBLEROUND",
            "  - EXT-CONTROLFLOW (experimental)",
            "- Unsupported operators:",
            "  - TRANSPOSE_CONV2D with dynamic weights and/or bias",
            "  - CONV3D with dynamic weights and/or bias",
        ]

    # pylint: disable=line-too-long
    def _u55_u65_tosa_support_lines() -> list[str]:
        return [
            "## Ethos-U55 and Ethos-U65 TOSA Summary Table",
            "",
            "The table below contains TOSA operators for Ethos-U55 and Ethos-U65.  ",
            "For TOSA operators, CPU fallback is not supported and unsupported operators/configurations result in a compiler error.  ",
            "",
            "| Operator | TOSA Constraints |",
            "| --- | --- |",
            "| ABS | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints) |",
            "| ADD | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints) |",
            "| ARGMAX | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints), [Specific](#ethos-u55-and-ethos-u65-tosa-argmax-constraints) |",
            "| ARITHMETIC_RIGHT_SHIFT | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints), [Specific](#ethos-u55-and-ethos-u65-tosa-arithmetic_right_shift-constraints) |",
            "| AVG_POOL2D | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints), [Specific](#ethos-u55-and-ethos-u65-tosa-avg_pool2d-constraints) |",
            "| CAST | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints), [Specific](#ethos-u55-and-ethos-u65-tosa-cast-constraints) |",
            "| CLAMP | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints) |",
            "| CLZ | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints) |",
            "| CONCAT | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints), [Specific](#ethos-u55-and-ethos-u65-tosa-concat-constraints) |",
            "| CONST | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints) |",
            "| CONV2D | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints), [Specific](#ethos-u55-and-ethos-u65-tosa-conv2d-constraints) |",
            "| CONV3D | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints), [Specific](#ethos-u55-and-ethos-u65-tosa-conv3d-constraints) |",
            "| IDENTITY | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints), [Specific](#ethos-u55-and-ethos-u65-tosa-identity-constraints) |",
            "| LOGICAL_LEFT_SHIFT | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints), [Specific](#ethos-u55-and-ethos-u65-tosa-logical_left_shift-constraints) |",
            "| MATMUL | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints), [Specific](#ethos-u55-and-ethos-u65-tosa-matmul-constraints) |",
            "| MAXIMUM | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints) |",
            "| MAX_POOL2D | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints), [Specific](#ethos-u55-and-ethos-u65-tosa-max_pool2d-constraints) |",
            "| MINIMUM | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints) |",
            "| MUL | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints) |",
            "| NEGATE | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints) |",
            "| PAD | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints), [Specific](#ethos-u55-and-ethos-u65-tosa-pad-constraints) |",
            "| REDUCE_MAX | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints), [Specific](#ethos-u55-and-ethos-u65-tosa-reduce_max-constraints) |",
            "| REDUCE_MIN | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints), [Specific](#ethos-u55-and-ethos-u65-tosa-reduce_min-constraints) |",
            "| REDUCE_SUM | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints), [Specific](#ethos-u55-and-ethos-u65-tosa-reduce_sum-constraints) |",
            "| RESCALE | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints), [Specific](#ethos-u55-and-ethos-u65-tosa-rescale-constraints) |",
            "| RESHAPE | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints), [Specific](#ethos-u55-and-ethos-u65-tosa-reshape-constraints) |",
            "| RESIZE | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints), [Specific](#ethos-u55-and-ethos-u65-tosa-resize-constraints) |",
            "| REVERSE | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints), [Specific](#ethos-u55-and-ethos-u65-tosa-reverse-constraints) |",
            "| SLICE | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints), [Specific](#ethos-u55-and-ethos-u65-tosa-slice-constraints) |",
            "| SUB | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints) |",
            "| TABLE | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints), [Specific](#ethos-u55-and-ethos-u65-tosa-table-constraints) |",
            "| TILE | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints), [Specific](#ethos-u55-and-ethos-u65-tosa-tile-constraints) |",
            "| TRANSPOSE | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints), [Specific](#ethos-u55-and-ethos-u65-tosa-transpose-constraints) |",
            "| TRANSPOSE_CONV2D | [Generic](#ethos-u55-and-ethos-u65-tosa-generic-constraints), [Specific](#ethos-u55-and-ethos-u65-tosa-transpose_conv2d-constraints) |",
            "",
            "## Ethos-U55 and Ethos-U65 TOSA Generic Constraints",
            "",
            "This is a list of constraints that most TOSA operators must satisfy in order to be scheduled on the NPU.",
            "",
            "- Baseline:",
            "  - TOSA Version: v1.0",
            "  - TOSA Profile: PRO-INT",
            "  - TOSA Level: 8k",
            "- PRO-INT extensions:",
            "  - EXT-INT16: 16-bit integer operations",
            "  - EXT-INT4: 4-bit integer weights",
            "  - EXT-DOUBLEROUND: double rounding support for RESCALE",
            "- Operators and configurations not listed as supported below are not supported.",
            "- For TOSA operators, CPU fallback is not supported.",
            "",
            "## Ethos-U55 and Ethos-U65 TOSA Specific Operator Constraints",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA ARITHMETIC_RIGHT_SHIFT Constraints",
            "",
            "This is a list of constraints that the ARITHMETIC_RIGHT_SHIFT operator must satisfy in order to be scheduled on the NPU.",
            "",
            "- DataTypes: int32 only (IFM and OFM must be 32-bit)",
            "- Round: true only",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA LOGICAL_LEFT_SHIFT Constraints",
            "",
            "This is a list of constraints that the LOGICAL_LEFT_SHIFT operator must satisfy in order to be scheduled on the NPU.",
            "",
            "- DataTypes: int32 only (IFM and OFM must be 32-bit)",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA TABLE Constraints",
            "",
            "This is a list of constraints that the TABLE operator must satisfy in order to be scheduled on the NPU.",
            "",
            "- DataTypes: int8 only",
            "- EXT-INT16 is not supported",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA AVG_POOL2D Constraints",
            "",
            "This is a list of constraints that the AVG_POOL2D operator must satisfy in order to be scheduled on the NPU.",
            "",
            "- Tensor axis: N=1 and HWC in range 1..65536",
            "- IF padding is used: 1 <= kernel_x <= 8 and 1 <= kernel_y <= 8",
            "- ELSE: 1 <= kernel_x*kernel_y <= 65536 and 1 <= kernel_y <= 256",
            "- Kernel stride: 1 <= stride <= 3",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA MAX_POOL2D Constraints",
            "",
            "This is a list of constraints that the MAX_POOL2D operator must satisfy in order to be scheduled on the NPU.",
            "",
            "- Tensor axis: N any, HWC in range 1..65536",
            "- Kernel size: 1 <= kernel_x*kernel_y <= 65536 and 1 <= kernel_y <= 256",
            "- Kernel stride: 1 <= stride <= 3",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA ARGMAX Constraints",
            "",
            "This is a list of constraints that the ARGMAX operator must satisfy in order to be scheduled on the NPU.",
            "",
            "- Tensor axis: N=1, C in range 1..127, and W*H in range 1..65536",
            "- Rank must be in range 1..4",
            "- Reduction axis must be channel",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA CONV2D Constraints",
            "",
            "This is a list of constraints that the CONV2D operator must satisfy in order to be scheduled on the NPU.",
            "",
            "- DataTypes (IFM, Weights, OFM): (i8_t,i8_t,i32_t), (i8_t,i4_t,i32_t), (i16_t,i8_t,i48_t)",
            "- EXT-INT16 is only supported if followed by RESCALE to i8_t, i16_t or i32_t",
            "- Tensor axis: NHW any, C in range 1..65536",
            "- Kernel size: 1 <= kernel_x*kernel_y <= 4096 and 1 <= kernel_y <= 64",
            "- Kernel stride: 1 <= stride <= 3",
            "- Sum of absolute weights must not exceed 127*65536",
            "- Bias must fit in signed int40: -549755813888..549755813887",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA CONV3D Constraints",
            "",
            "This is a list of constraints that the CONV3D operator must satisfy in order to be scheduled on the NPU.",
            "",
            "- DataTypes (IFM, Weights, OFM): (i8_t,i8_t,i32_t), (i8_t,i4_t,i32_t), (i16_t,i8_t,i48_t)",
            "- EXT-INT16 is only supported if followed by RESCALE to i8_t, i16_t or i32_t",
            "- Tensor axis: NDHW any, C in range 1..65536",
            "- Kernel size: 1 <= kernel_x*kernel_y <= 4096 and 1 <= kernel_y <= 64 and kernel_z==1",
            "- Kernel stride: 1 <= stride_x <= 3, 1 <= stride_y <= 3, 1 <= stride_z",
            "- Sum of absolute weights must not exceed 127*65536",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA TRANSPOSE_CONV2D Constraints",
            "",
            "This is a list of constraints that the TRANSPOSE_CONV2D operator must satisfy in order to be scheduled on the NPU.",
            "",
            "- DataTypes (IFM, Weights, OFM): (i8_t,i8_t,i32_t), (i8_t,i4_t,i32_t), (i16_t,i8_t,i48_t)",
            "- EXT-INT16 is only supported if followed by RESCALE to i8_t, i16_t or i32_t",
            "- Tensor axis: N any, HWC in range 1..65536",
            "- Kernel size: 1 <= kernel_x*kernel_y <= 4096 and 1 <= kernel_y <= 64",
            "- Stride combinations:",
            "  - (1,1), (2,2)",
            "  - (1,2) only if IFM height and kernel height are 1",
            "  - (2,1) only if IFM width and kernel width are 1",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA MATMUL Constraints",
            "",
            "This is a list of constraints that the MATMUL operator must satisfy in order to be scheduled on the NPU.",
            "",
            "- DataTypes (IFM, IFM2, OFM): (i8_t, i8_t, i32_t)",
            "- Tensor axis: WC in range 1..65536, NH any",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA RESIZE Constraints",
            "",
            "This is a list of constraints that the RESIZE operator must satisfy in order to be scheduled on the NPU.",
            "",
            "- Upscale must be power-of-two in range 2x..8x",
            "- Bilinear mode must be followed by RESCALE with shift=log2(upscale_x*upscale_y) and scale=1",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA TRANSPOSE Constraints",
            "",
            "This is a list of constraints that the TRANSPOSE operator must satisfy in order to be scheduled on the NPU.",
            "",
            "- DataTypes: int8, int16, int32 (bool is not supported)",
            "- Rank: int8/int16 support 1..MAX_RANK, int32 support 1..4",
            "- Permutation and tensor-axis constraints:",
            "  - IF IFM is Int32:",
            "    - Rank must be <= 4",
            "    - NHWC: C <= 2^15",
            "    - NWHC: N == 1, H <= 2^16, W <= 2^16, C <= 2^14",
            "    - NHCW: N*H <= 2^16, W <= 2^16, C <= 2^16",
            "    - NCWH: N == 1, H <= 2^16, W <= 2^16, C <= 2^14",
            "    - Any other permutation vector is unsupported",
            "  - IF IFM is Int8 or Int16:",
            "    - NHWC: no shape constraints",
            "    - ELSE IF Rank <= 4 and permutation is NWHC/NHCW/NCWH:",
            "      - (N*H, W, C) <= (2^16, 2^16, 2^16)",
            "    - ELSE: product of elements must be <= 2^16",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA REDUCE_SUM Constraints",
            "",
            "This is a list of constraints that the REDUCE_SUM operator must satisfy in order to be scheduled on the NPU.",
            "",
            "- Reduced axis must be in range 1..65536",
            "- IF reduction is over channel: other axes can be any size",
            "- ELSE: product of axes before and after the reduced axis respectively must be in range 1..65536",
            "- Reduction is native over depth only; otherwise Vela inserts transpose (lower performance)",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA REDUCE_MAX Constraints",
            "",
            "This is a list of constraints that the REDUCE_MAX operator must satisfy in order to be scheduled on the NPU.",
            "",
            "- DataTypes: INT8->INT8 and INT16->INT16",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA REDUCE_MIN Constraints",
            "",
            "This is a list of constraints that the REDUCE_MIN operator must satisfy in order to be scheduled on the NPU.",
            "",
            "- DataTypes: INT8->INT8 and INT16->INT16",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA CONCAT Constraints",
            "",
            "This is a list of constraints that the CONCAT operator must satisfy in order to be scheduled on the NPU.",
            "",
            "- DataTypes: INT8, INT16, INT32",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA RESHAPE Constraints",
            "",
            "This is a list of constraints that the RESHAPE operator must satisfy in order to be scheduled on the NPU.",
            "",
            "- DataTypes: INT8, INT16, INT32",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA SLICE Constraints",
            "",
            "This is a list of constraints that the SLICE operator must satisfy in order to be scheduled on the NPU.",
            "",
            "- DataTypes: INT8, INT16, INT32",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA TILE Constraints",
            "",
            "This is a list of constraints that the TILE operator must satisfy in order to be scheduled on the NPU.",
            "",
            "- DataTypes: INT8, INT16, INT32",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA CAST Constraints",
            "",
            "This is a list of constraints that the CAST operator must satisfy in order to be scheduled on the NPU.",
            "",
            "- Any PRO-INT combination is supported if bool is not used",
            "- Down-casting from int32 is not supported",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA RESCALE Constraints",
            "",
            "This is a list of constraints that the RESCALE operator must satisfy in order to be scheduled on the NPU.",
            "",
            "- EXT-INT16 (INT48 types) are not supported standalone, but might be supported if the compiler can fuse them. "
            "See EXT-INT16 constraints on other operators for more details.",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA IDENTITY Constraints",
            "",
            "This is a list of constraints that the IDENTITY operator must satisfy in order to be scheduled on the NPU.",
            "",
            "- Supported pairs: INT8->INT8, INT16->INT16, INT32->INT32, INT48->INT48",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA PAD Constraints",
            "",
            "This is a list of constraints that the PAD operator must satisfy in order to be scheduled on the NPU.",
            "",
            "- Supported pairs: INT8->INT8, INT16->INT16, INT32->INT32",
            "- Bool is not supported",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA REVERSE Constraints",
            "",
            "This is a list of constraints that the REVERSE operator must satisfy in order to be scheduled on the NPU.",
            "",
            "- Supported pairs: INT8->INT8 and INT16->INT16",
            "- Only W-axis and H-axis reverse are supported",
            "- INT32 is not supported",
            "",
            "### Ethos-U55 and Ethos-U65 TOSA Not Supported Operators",
            "",
            "This is a list of TOSA operators that are not supported on Ethos-U55 and Ethos-U65.",
            "",
            "- BITWISE_AND",
            "- BITWISE_OR",
            "- BITWISE_XOR",
            "- BITWISE_NOT",
            "- INTDIV",
            "- LOGICAL_AND",
            "- LOGICAL_RIGHT_SHIFT",
            "- LOGICAL_OR",
            "- LOGICAL_XOR",
            "- LOGICAL_NOT",
            "- SELECT",
            "- EQUAL",
            "- GREATER",
            "- GREATER_EQUAL",
            "- REDUCE_ALL",
            "- REDUCE_ANY",
            "- GATHER",
            "- SCATTER",
        ]

    # pylint: enable=line-too-long

    def _regor_generic_constraints(supported_ops):
        # extract generic constraints from Regor supported-ops
        # A generic constraint applies to all opTypes except a list of exceptions
        # a constraint is considered generic if it applies to more than 80% of the opTypes
        threshold = 0.20 * len(supported_ops)
        allconstraints = set()
        exceptions = dict()
        for op in supported_ops:
            for constraint in supported_ops[op]:
                allconstraints.add(constraint)
                exceptions[constraint] = list()
        for op in supported_ops:
            for constraint in [c for c in allconstraints if c not in supported_ops[op]]:
                exceptions[constraint].append(op)
        return {k: v for k, v in exceptions.items() if len(v) < threshold}

    def _regor_specific_constraints(supported_ops, generic_constraints):
        # extract specific constraints from Regor supported-ops
        # A constraint is considered op-specific if it's not in the generic constraints
        specific_constraints = dict()
        # create op-specific constraints
        for op in sorted(supported_ops):
            constraints = supported_ops[op]
            # Regor renames ARG_MAX to avoid name-collisions
            op = op.replace("ARGMAX", "ARG_MAX")
            specific_constraints[op] = sorted(filter(lambda c: c not in generic_constraints, constraints))
        return specific_constraints

    supported_ops_u55_u65 = regor.tflite_operator_constraints("EthosU55")
    u55_u65_generic = _regor_generic_constraints(supported_ops_u55_u65)
    u55_u65_specific = _regor_specific_constraints(supported_ops_u55_u65, u55_u65_generic)

    supported_ops_u85 = regor.tflite_operator_constraints("EthosU85")
    u85_generic = _regor_generic_constraints(supported_ops_u85)
    u85_specific = _regor_specific_constraints(supported_ops_u85, u85_generic)

    # Add license for supported ops
    lines = [
        "<!--",
        "SPDX-FileCopyrightText: Copyright 2020-2026 Arm Limited and/or its affiliates <open-source-office@arm.com>",
        "",
        "SPDX-License-Identifier: Apache-2.0",
        "",
        "Licensed under the Apache License, Version 2.0 (the License); you may",
        "not use this file except in compliance with the License.",
        "You may obtain a copy of the License at",
        "",
        "www.apache.org/licenses/LICENSE-2.0",
        "",
        "Unless required by applicable law or agreed to in writing, software",
        "distributed under the License is distributed on an AS IS BASIS, WITHOUT",
        "WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.",
        "See the License for the specific language governing permissions and",
        "limitations under the License.",
        "-->",
        "",
    ]

    lines += [
        "# Supported Ops",
        "",
        "This file was automatically generated by Vela using the `--supported-ops-report` parameter.  ",
        f"Vela version: `{__version__}`",
        "",
        "This file complies with",
        "[**Gitiles Markdown syntax**](https://gerrit.googlesource.com/gitiles/+/HEAD/Documentation/markdown.md)",
        "",
        "Summary table of constraints for:",
    ]

    # Ethos-U55, Ethos-U65 and Ethos-U85 TFLite summary links
    lines += [
        "- [Ethos-U55 and Ethos-U65 TFLite](#ethos-u55-and-ethos-u65-tflite-summary-table)",
        "- [Ethos-U85 TFLite](#ethos-u85-tflite-summary-table)",
        "- [Ethos-U55 and Ethos-U65 TOSA](#ethos-u55-and-ethos-u65-tosa-operator-support)",
        "- [Ethos-U85 TOSA](#ethos-u85-tosa-operator-support)",
    ]

    # Ethos-U55 and Ethos-U65 TFLite Summary Table
    lines += [
        "",
        "## Ethos-U55 and Ethos-U65 TFLite Summary Table",
        "",
    ]
    lines += [
        "The table below contains TFLite operators that can be placed on the Ethos-U55 and Ethos-U65.  ",
        "If the constraints are not met, then that operator will be scheduled on the CPU instead.  ",
        "For any other TFLite operator not listed, will be left untouched and scheduled on the CPU.  ",
        "Please check the supported operator list for your chosen runtime for further information.",
        "",
        "| Operator | TFLite Constraints |",
        "| --- | --- |",
    ]
    for op in u55_u65_specific:
        sconstraints = u55_u65_specific[op]
        links = "[Generic](#ethos-u55-and-ethos-u65-tflite-generic-constraints)"
        if len(sconstraints):
            links += f", [Specific](#ethos-u55-and-ethos-u65-tflite-{op.lower()}-constraints)"
        lines.append(f"| {op.upper()} | {links} |")

    # Ethos-U85 TFLite Summary Table
    lines += [
        "",
        "## Ethos-U85 TFLite Summary Table",
        "",
    ]
    lines += [
        "The table below contains TFLite operators that can be placed on the Ethos-U85.  ",
        "If the constraints are not met, then that operator will be scheduled on the CPU instead.  ",
        "For any other TFLite operator not listed, will be left untouched and scheduled on the CPU.  ",
        "Please check the supported operator list for your chosen runtime for further information.  ",
        "",
        "| Operator | TFLite Constraints |",
        "| --- | --- |",
    ]
    for op in u85_specific:
        sconstraints = u85_specific[op]
        links = "[Generic](#ethos-u85-tflite-generic-constraints)"
        if len(sconstraints):
            links += f", [Specific](#ethos-u85-tflite-{op.lower()}-constraints)"
        lines.append(f"| {op.upper()} | {links} |")

    # Ethos-U55 and Ethos-U65 generic constraints
    lines += [
        "",
        "## Ethos-U55 and Ethos-U65 TFLite Generic Constraints",
        "",
        "This is a list of constraints that most operators must satisfy in order to be scheduled on the NPU.  ",
        "(Operators excluded from certain constraints are listed as exceptions )\n" "",
    ]
    for constraint in u55_u65_generic:
        exceptions = u55_u65_generic[constraint]
        lines.append(f"- {constraint}")
        if len(exceptions):
            lines.append(f"  - Exceptions: [{', '.join(sorted(exceptions))}]")

    lines += ["", "## Ethos-U55 and Ethos-U65 Specific Operator constraints"]
    for name in u55_u65_specific:
        constraints = u55_u65_specific[name]
        if not len(constraints):
            continue
        lines += [
            "",
            f"### Ethos-U55 and Ethos-U65 TFLite {name.upper()} Constraints",
            "",
            f"This is a list of constraints that the {name.upper()} operator "
            "must satisfy in order to be scheduled on the"
            " NPU.",
            "",
        ]
        for constraint in constraints:
            lines.append(f"- {constraint}")

    # Generic TFLite constraints for Ethos-U85
    lines += [
        "",
        "## Ethos-U85 TFLite Generic Constraints",
        "",
        "This is a list of constraints that most operators must satisfy in order to be scheduled on the NPU.  ",
        "(Operators excluded from certain constraints are listed as exceptions )\n" "",
        "",
    ]
    for constraint in u85_generic:
        exceptions = u85_generic[constraint]
        lines.append(f"- {constraint}")
        if len(exceptions):
            lines.append(f"  - Exceptions: [{', '.join(sorted(exceptions))}]")

    # Op-specific TFLite constraints for Ethos-U85
    lines += [
        "",
        "## Ethos-U85 Specific Operator Constraints",
    ]

    for name in u85_specific:
        constraints = u85_specific[name]
        if not len(constraints):
            continue
        lines += [
            "",
            f"### Ethos-U85 TFLite {name.upper()} Constraints",
            "",
            f"This is a list of constraints that the {name.upper()} operator "
            "must satisfy in order to be scheduled on the"
            " NPU.",
            "",
        ]
        for constraint in constraints:
            lines.append(f"- {constraint}")

    # Ethos-U55 and Ethos-U65 TOSA operator support
    lines += [
        "",
        "## Ethos-U55 and Ethos-U65 TOSA Operator Support",
        "",
    ]
    lines += _u55_u65_tosa_support_lines()
    lines += [""] + _u85_tosa_support_lines()

    # Note. this will generate the file in the CWD
    filepath = os.path.join(os.getcwd(), "SUPPORTED_OPS.md")
    with open(filepath, "wt") as md:
        md.writelines(line + "\n" for line in lines)
        print(f"Report file: {filepath}")


def get_compiler_config(
    enable_debug_db: bool,
    verbose_all: bool,
    verbose_high_level_command_stream: bool,
    verbose_register_command_stream: bool,
    optimize: str,
    arena_cache_size: int,
    verbose_schedule: bool,
    verbose_allocation: bool,
    verbose_graph: bool,
    verbose_quantization: bool,
    verbose_packing: bool,
    verbose_performance: bool,
    show_cpu_operations: bool,
    output_format: str,
    disable_chaining: bool,
    disable_fwd: bool,
    disable_cascading: bool,
    disable_buffering: bool,
    disable_ifm_reuse: bool,
    cop_format: str,
    separate_io_regions: bool,
    cpu_tensor_alignment: int,
    tensor_allocator: str,
) -> str:
    """Build compiler config file."""
    config = "\n[compiler]\n"
    if verbose_high_level_command_stream:
        config += "verbose_high_level_command_stream=true\n"
    if verbose_register_command_stream:
        config += "verbose_register_command_stream=true\n"
    if verbose_performance or show_cpu_operations or enable_debug_db:
        config += "enable_db=true\n"
    if output_format == "raw":
        config += "output_format=Raw\n"
    else:
        config += "output_format=TFLite\n"
    config += f"cop_format={cop_format}\n"

    config += "\n[scheduler]\n"
    config += f"optimize={optimize}\n"
    config += f"arena_size_limit={arena_cache_size}\n"
    if verbose_schedule:
        config += "verbose=true\n"
    if verbose_allocation:
        config += "verbose_allocation=true\n"
    config += "disable_feature="
    if disable_chaining:
        config += "Grouping|"
    if disable_fwd:
        config += "FWD|"
    if disable_cascading:
        config += "Cascading|"
    if disable_buffering:
        config += "WeightBuffering|"
    if disable_ifm_reuse:
        config += "ReuseIFM|"
    config = config.rstrip("|") + "\n"
    if separate_io_regions:
        config += "separate_io_regions=true\n"
    config += f"cpu_tensor_alignment={cpu_tensor_alignment}\n"
    config += f"tensor_allocator={tensor_allocator}\n"

    config += "\n[graph]\n"
    if verbose_graph:
        config += "verbose=true\n"
    if verbose_quantization:
        config += "verbose_quantization=true\n"

    return config


def get_format(in_data: mmap.mmap) -> str:
    """Infere format based on input file."""
    ret = "UNDEFINED"
    if in_data.size() < 8:
        return ret
    second_word = int.from_bytes(in_data[4:8], "little")
    if second_word == TFLITE_MAGIC:
        ret = "TFLITE"
    if second_word == TOSA_MAGIC:
        ret = "TOSA"
    return ret


def list_config_files():
    print("Available config files:")
    path_length = len(architecture_features.CONFIG_FILES_PATH + os.path.sep)
    for config in glob.glob(os.path.join(architecture_features.CONFIG_FILES_PATH, "*", "*.ini")):
        print(config[path_length:])


def list_configs(config_filename):
    vela_config_filename = config_filename
    if not os.path.isfile(vela_config_filename):
        vela_config_filename = os.path.join(architecture_features.CONFIG_FILES_PATH, config_filename)
        if not os.path.isfile(vela_config_filename):
            assert False, f"Cannot find config file {config_filename}"

    if os.path.splitext(vela_config_filename)[1] != ".ini":
        assert False, f"Specified file {config_filename} is not a Vela config file"

    print(f"Configurations defined in {vela_config_filename}:")
    vela_config = ConfigParser()
    vela_config.read(vela_config_filename)
    for section in vela_config.sections():
        print(f"   {section}")


class DeprecatedStoreAction(argparse.Action):
    def __init__(self, option_strings, dest, **kwargs):
        super().__init__(option_strings, dest, **kwargs)

    def __call__(self, parser, namespace, values, option_string=None):
        print(f"Warning: Option {option_string} is deprecated and will be removed in a future release.")
        setattr(namespace, self.dest, values)


class DeprecatedStoreTrueAction(argparse.Action):
    def __init__(self, option_strings, dest, **kwargs):
        super().__init__(option_strings, dest, nargs=0, **kwargs)

    def __call__(self, parser, namespace, values, option_string=None):
        print(f"Warning: Option {option_string} is deprecated and will be removed in a future release.")
        setattr(namespace, self.dest, True)


def main(argv: Optional[List[str]] = None) -> int:
    """Run the main entry point."""
    if argv is None:
        argv = sys.argv[1:]

    parser = argparse.ArgumentParser(prog="vela", description="Neural network model compiler for Arm Ethos-U NPUs")
    parser.register("action", "deprecated_store", DeprecatedStoreAction)
    parser.register("action", "deprecated_store_true", DeprecatedStoreTrueAction)
    parser.add_argument("--version", action="version", version=__version__)
    parser.add_argument(
        "--api-version",
        action="version",
        version="Warning: Option --api-version is deprecated and will be removed in a future release."
        f" Version: {API_VERSION}",
        help="[DEPRECATED] Displays the version of the external API.",
    )
    parser.add_argument(
        "--supported-ops-report",
        action="store_true",
        help="Generate the SUPPORTED_OPS.md file in the current working directory and exit",
    )

    parser.add_argument(
        "--list-config-files",
        action="store_true",
        help=(
            "Display all available configurations in the `config_files` folder and exit. To select config file, "
            "use the --config argument with one of the listed config files (For example: --config Arm/vela.ini )"
        ),
    )
    parser.add_argument(
        "--list-configs",
        type=str,
        help=("Display all configurations defined in the specified config file"),
    )

    # set network nargs to be optional to allow the support-ops-report CLI option to be used standalone
    parser.add_argument(
        "network",
        metavar="NETWORK",
        type=str,
        nargs="?",
        default=None,
        help="Filename of the input network",
    )
    parser.add_argument(
        "--output-dir", type=str, default="output", help="Output directory to write files to (default: %(default)s)"
    )
    parser.add_argument(
        "--output-format",
        type=str,
        default="tflite",
        choices=["tflite", "raw"],
        help="Output format (default: %(default)s).",
    )
    parser.add_argument(
        "--enable-debug-db",
        action="store_true",
        default=None,
        help="Enables the calculation and writing of a network debug database to output directory",
    )
    parser.add_argument(
        "--config",
        type=str,
        action="append",
        help="Vela configuration file(s) in Python ConfigParser .ini file format",
    )
    parser.add_argument("--verbose-all", action="store_true", help="Enable all verbose options")
    parser.add_argument(
        "--verbose-config", action="store_true", help="Enable system configuration and memory mode debug"
    )
    parser.add_argument("--verbose-graph", action="store_true", help="Enable graph optimizer debug")
    parser.add_argument("--verbose-quantization", action="store_true", help="Enable quantization debug")
    parser.add_argument(
        "--verbose-packing", action="deprecated_store_true", help="[DEPRECATED] Enable pass packing debug"
    )
    parser.add_argument(
        "--verbose-tensor-purpose", action="deprecated_store_true", help="[DEPRECATED] Enable tensor purpose debug"
    )
    parser.add_argument("--verbose-tensor-format", action="store_true", help="Enable tensor format debug")
    parser.add_argument("--verbose-schedule", action="store_true", help="Enable schedule debug")
    parser.add_argument("--verbose-allocation", action="store_true", help="Enable tensor allocation debug")
    parser.add_argument(
        "--verbose-high-level-command-stream", action="store_true", help="Enable high level command stream debug"
    )
    parser.add_argument(
        "--verbose-register-command-stream", action="store_true", help="Enable register command stream debug"
    )
    parser.add_argument(
        "--verbose-operators", action="deprecated_store_true", help="[DEPRECATED] Enable operator list debug"
    )
    parser.add_argument("--verbose-weights", action="store_true", help="Enable weights information debug")
    parser.add_argument("--verbose-cycle-estimate", action="store_true", help="Enable cycle estimate information debug")
    parser.add_argument("--verbose-performance", action="store_true", help="Enable performance information debug")
    parser.add_argument(
        "--verbose-progress", action="deprecated_store_true", help="[DEPRECATED] Enable progress information debug"
    )
    parser.add_argument(
        "--show-cpu-operations", action="store_true", help="Show the operations that fall back to the CPU"
    )
    parser.add_argument(
        "--timing", action="deprecated_store_true", help="[DEPRECATED] Time the compiler doing operations"
    )
    parser.add_argument(
        "--force-symmetric-int-weights",
        action="store_true",
        help="Forces all zero points to 0 for signed integer weights",
    )
    parser.add_argument(
        "--accelerator-config",
        type=str,
        default="ethos-u55-256",
        choices=list(architecture_features.Accelerator.member_list()),
        help="Accelerator configuration to use (default: %(default)s)",
    )
    parser.add_argument(
        "--system-config",
        type=str,
        default=architecture_features.ArchitectureFeatures.DEFAULT_CONFIG,
        help="System configuration to select from the Vela configuration file (default: %(default)s)",
    )
    parser.add_argument(
        "--memory-mode",
        type=str,
        default=architecture_features.ArchitectureFeatures.DEFAULT_CONFIG,
        help="Memory mode to select from the Vela configuration file (default: %(default)s)",
    )
    parser.add_argument(
        "--tensor-allocator",
        default=TensorAllocator.HillClimb,
        type=lambda s: TensorAllocator[s],
        choices=list(TensorAllocator),
        help="Tensor Allocator algorithm (default: %(default)s)",
    )
    parser.add_argument(
        "--show-subgraph-io-summary",
        action="deprecated_store_true",
        help="[DEPRECATED] Shows a summary of all the subgraphs and their inputs and outputs",
    )
    parser.add_argument(
        "--max-block-dependency",
        action="deprecated_store",
        type=int,
        default=architecture_features.ArchitectureFeatures.MAX_BLOCKDEP,
        choices=range(0, architecture_features.ArchitectureFeatures.MAX_BLOCKDEP + 1),
        help=(
            "[DEPRECATED] Set the maximum value that can be used for the block dependency between npu kernel operations"
            " (default: %(default)s)"
        ),
    )
    parser.add_argument(
        "--optimise",
        type=lambda s: scheduler.OptimizationStrategy[s],
        default=scheduler.OptimizationStrategy.Performance,
        choices=list(scheduler.OptimizationStrategy),
        help=(
            "Set the optimisation strategy. The Size strategy results in minimal SRAM usage (does not use"
            " arena-cache-size). The Performance strategy results in maximal performance (uses the arena-cache-size"
            " if specified) (default: %(default)s)"
        ),
    )
    parser.add_argument(
        "--arena-cache-size",
        type=int,
        help=(
            "Set the size of the arena cache memory area, in bytes. If specified, this option overrides the memory"
            " mode attribute with the same name in a Vela configuration file"
        ),
    )
    parser.add_argument(
        "--cpu-tensor-alignment",
        type=int,
        default=Tensor.AllocationQuantum,
        help=(
            "Controls the allocation byte alignment of CPU tensors including Ethos-U Custom"
            " operator inputs and outputs (default: %(default)s Bytes)"
        ),
    )
    parser.add_argument(
        "--recursion-limit",
        action="deprecated_store",
        type=int,
        default=1000,
        help=(
            "[DEPRECATED] Set the recursion depth limit, may result in RecursionError"
            " if too low (default: %(default)s)"
        ),
    )
    parser.add_argument(
        "--hillclimb-max-iterations",
        action="deprecated_store",
        type=int,
        default=HillClimbAllocator.MAX_ITERATIONS,
        help=(
            "[DEPRECATED] Set the maximum number of iterations the Hill Climb tensor allocator"
            " will run (default: %(default)s)"
        ),
    )
    parser.add_argument(
        "--cop-format",
        choices=["COP1", "COP2"],
        default="COP1",
    )
    parser.add_argument(
        "--separate-io-regions",
        action="store_true",
        help="Use separate regions for input and output tensors (implies COP2 driver actions format)",
    )

    # debug options
    parser.add_argument(
        "--debug-force-legacy-core",
        action="deprecated_store_true",
        help="Debug: Use the deprecated legacy Python compilation core",
    )
    parser.add_argument(
        "--debug-force-regor", action="deprecated_store_true", help="[DEPRECATED] Debug: Force the use of the regor"
    )
    parser.add_argument("--disable-chaining", action="deprecated_store_true", default=False, help="[DEPRECATED]")
    parser.add_argument("--disable-fwd", action="deprecated_store_true", default=False, help="[DEPRECATED]")
    parser.add_argument("--disable-cascading", action="deprecated_store_true", default=False, help="[DEPRECATED]")
    parser.add_argument("--disable-buffering", action="deprecated_store_true", default=False, help="[DEPRECATED]")
    parser.add_argument("--disable-ifm-reuse", action="deprecated_store_true", default=False, help="[DEPRECATED]")
    args = parser.parse_args(argv)

    # Generate the supported ops report and exit
    if args.supported_ops_report:
        generate_supported_ops()
        return 0

    if args.list_config_files:
        list_config_files()
        return 0

    if args.list_configs:
        list_configs(args.list_configs)
        return 0

    if args.network is None:
        parser.error("the following argument is required: NETWORK")

    if args.cop_format == "COP1" and args.separate_io_regions:
        parser.error("Driver actions format 'COP2' is required for --separate-io-regions")

    def _parse_config(config):
        # Make sure the correct separator is used depending on OS
        config = os.path.normpath(config)

        if os.path.splitext(config)[1] != ".ini":
            raise InputFileError(config, "Configuration files must use the .ini extension")

        if (
            len(config.split(os.path.sep)) == 2
            and not config.startswith(os.path.sep)
            and not config.startswith(".")
            and not config.startswith("~")
        ):
            config_path = os.path.join(architecture_features.CONFIG_FILES_PATH, config)
        else:
            # Check if the configuration file is correctly placed inside the config_files directory
            if os.access(
                os.path.join(architecture_features.CONFIG_FILES_PATH, *config.split(os.path.sep)[-2:]),
                os.R_OK,
            ):
                rel_path = os.path.join(*config.split(os.path.sep)[-2:])
                print(
                    f"Warning: Consider accessing the configuration by --config {rel_path} since it is located "
                    "inside the config_files directory."
                )
            config_path = config

        if not os.access(config_path, os.R_OK):
            raise InputFileError(
                config_path,
                "File not found or is not readable. The configuration file is either not located in a folder "
                "directly under the `config_files` directory or its path has not been provided correctly.",
            )

        return config_path

    # check all config files exist because they will be read as a group
    config_files = [_parse_config(cfg) for cfg in args.config] if args.config else None

    if args.cpu_tensor_alignment < 16 or args.cpu_tensor_alignment & (args.cpu_tensor_alignment - 1) != 0:
        parser.error(
            "Invalid argument to --cpu-tensor-alignment = {} (must be greater than or equal to 16 and a power of 2)"
            "".format(args.cpu_tensor_alignment)
        )

    if args.debug_force_legacy_core and args.debug_force_regor:
        parser.error(
            "Cannot force the use of both the deprecated legacy Python compilation core and the Regor C++ compilation"
            " core at the same time. Please choose only one of these options."
        )

    if args.verbose_all:
        for v in vars(args):
            if v.startswith("verbose") and v != "verbose_all":
                setattr(args, v, True)

    sys.setrecursionlimit(args.recursion_limit)

    arch = architecture_features.ArchitectureFeatures(
        vela_config_files=config_files,
        system_config=args.system_config,
        memory_mode=args.memory_mode,
        accelerator_config=args.accelerator_config,
        max_blockdep=args.max_block_dependency,
        verbose_config=args.verbose_config,
        arena_cache_size=args.arena_cache_size,
    )

    model_reader_options = model_reader.ModelReaderOptions()

    # Vela's legacy Python compilation core has been deprecated.
    # However, this can be overridden to still be used for TFLite and Ethos-U55 or Ethos-U65 by using the
    # `--debug-force-legacy-core` option, until fully removed.
    # All Ethos-U85 or TOSA network compilations always use Vela's C++ compilation core (Regor).
    if arch.is_ethos_u85_system or args.network.lower().endswith(".tosa") or not args.debug_force_legacy_core:
        if args.debug_force_legacy_core:
            parser.error(
                "Forcing the use of the deprecated legacy Python compilation core is not possible for this target"
                " system or input network. \nThe legacy core only supports Ethos-U55 and Ethos-U65 systems and TFLite"
                " input networks. Please remove the --debug-force-legacy-core option and try again."
            )
        system_config = "[architecture]\n"
        system_config += f"macs={arch.num_macs_per_cycle}\n"
        system_config += f"cores={arch.ncores}\n"

        system_config += "[vela]\n"
        system_config += f"system_config_name={arch.system_config}\n"
        system_config += f"memory_mode_name={arch.memory_mode}\n"

        # TODO MLBEDSW-8190: Improve support for multiple config files
        for config_file in arch.vela_config_files:
            with open(config_file, "rb") as f:
                system_config += f.read().decode("utf-8")

        # Transform Vela accelerator config string format to Regor format
        accelerator_config = args.accelerator_config
        substrings = accelerator_config.split("-")
        substrings = [substring.capitalize() for substring in substrings[:-1]]
        accelerator = "".join(substrings)

        options = get_compiler_config(
            args.enable_debug_db,
            args.verbose_all,
            args.verbose_high_level_command_stream,
            args.verbose_register_command_stream,
            args.optimise,
            # TODO MLBEDSW-8191: Clean up arena_cache_size selection
            # arch.arena_cache_size will use value from CLI option if available, otherwise from config file
            arch.arena_cache_size,
            args.verbose_schedule,
            args.verbose_allocation,
            args.verbose_graph,
            args.verbose_quantization,
            args.verbose_packing,
            args.verbose_performance,
            args.show_cpu_operations,
            args.output_format,
            args.disable_chaining,
            args.disable_fwd,
            args.disable_cascading,
            args.disable_buffering,
            args.disable_ifm_reuse,
            args.cop_format,
            args.separate_io_regions,
            args.cpu_tensor_alignment,
            args.tensor_allocator,
        )

        process_regor(
            arch,
            args.network,
            accelerator,
            system_config,
            options,
            args.enable_debug_db,
            args.output_dir,
            args.output_format,
            args.verbose_weights,
            args.verbose_cycle_estimate,
            args.show_cpu_operations,
            args.verbose_performance,
        )

    else:
        compiler_options = compiler_driver.CompilerOptions(
            verbose_graph=args.verbose_graph,
            verbose_quantization=args.verbose_quantization,
            verbose_packing=args.verbose_packing,
            verbose_tensor_purpose=args.verbose_tensor_purpose,
            verbose_tensor_format=args.verbose_tensor_format,
            verbose_allocation=args.verbose_allocation,
            verbose_high_level_command_stream=args.verbose_high_level_command_stream,
            verbose_register_command_stream=args.verbose_register_command_stream,
            verbose_operators=args.verbose_operators,
            verbose_weights=args.verbose_weights,
            verbose_cycle_estimate=args.verbose_cycle_estimate,
            verbose_performance=args.verbose_performance,
            verbose_progress=args.verbose_progress,
            show_cpu_operations=args.show_cpu_operations,
            tensor_allocator=args.tensor_allocator,
            timing=args.timing,
            force_symmetric_int_weights=args.force_symmetric_int_weights,
            output_dir=args.output_dir,
            cpu_tensor_alignment=args.cpu_tensor_alignment,
            hillclimb_max_iterations=args.hillclimb_max_iterations,
        )

        scheduler_options = scheduler.SchedulerOptions(
            optimization_strategy=args.optimise,
            sram_target=arch.arena_cache_size,
            verbose_schedule=args.verbose_schedule,
            verbose_progress=args.verbose_progress,
        )

        try:
            nng = process(
                args.network,
                args.enable_debug_db,
                arch,
                model_reader_options,
                compiler_options,
                scheduler_options,
                args.output_format,
            )

        except VelaError as e:
            print(e.data)
            return 1

        if args.show_subgraph_io_summary:
            print_subgraph_io_summary(nng)

    return 0
