#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: Copyright 2020-2024 Arm Limited and/or its affiliates <open-source-office@arm.com>
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
import os
import sys
import time
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
from .nn_graph import NetworkType
from .nn_graph import TensorAllocator
from .tensor import MemArea
from .tensor import Tensor
from .tflite.Model import Model
from .tflite_mapping import builtin_operator_map
from .tflite_mapping import builtin_operator_name_map
from .tflite_mapping import optype_to_builtintype
from .tflite_model_semantic import TFLiteSemantic
from .tflite_supported_operators import TFLiteSupportedOperators
from .tosa_model_semantic import TosaSemantic
from .tosa_supported_operators import TosaSupportedOperators
from ethosu import regor

TFLITE_MAGIC = 0x334C4654
TOSA_MAGIC = 0x41534F54


def process(input_name, enable_debug_db, arch, model_reader_options, compiler_options, scheduler_options):
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
    if input_name.endswith(".tflite"):
        tflite_writer.write_tflite(nng, output_tfl_filename)
    if input_name.endswith(".tosa"):
        rawdata_writer.write_rawdata_output(nng, arch, output_basename)

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
    verbose_weights=False,
    verbose_cycle_estimate=False,
    show_cpu_operations=False,
    verbose_performance=False,
):
    os.makedirs(output_dir, exist_ok=True)

    with open(input_name, "rb") as f:
        network = f.read()
    fmt = get_format(network)

    compiled_model = regor.compile(accelerator, network, fmt, system_config, options=options, verbose=True)

    model_name = os.path.splitext(os.path.basename(input_name))[0]

    output_basename = os.path.join(output_dir, model_name)

    if isinstance(compiled_model, regor.CompiledTFLiteModel):
        output_name = output_basename + "_vela.tflite"
        with open(output_name, "wb") as f:
            f.write(compiled_model.model)
    elif isinstance(compiled_model, regor.CompiledRawModel):
        rawdata_writer.write_rawdata_output_from_model(output_basename + "_vela.npz", compiled_model)

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
    # Exclude network type from generation by adding value to exclude list.
    # To easily exclude NetworkType from generated documentation.
    exclude_generation_network_type_value = [NetworkType.TOSA.value]

    def _exclude_list_names(constraint, exclude_list):
        constraints_excluded_names = [
            optype_to_builtintype(op) for op, exclude_constraint in exclude_list if constraint in exclude_constraint
        ]
        return f" - [{', '.join(sorted(constraints_excluded_names))}]" if constraints_excluded_names else ""

    # Add license for supported ops
    lines = [
        "<!--",
        "SPDX-FileCopyrightText: Copyright 2020-2024 Arm Limited and/or its affiliates <open-source-office@arm.com>",
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

    # Ethos-U55 and Ethos-U65 TFLite and TOSA
    for network_type in NetworkType:
        if network_type.value in exclude_generation_network_type_value:
            continue

        lines += [
            f"- [Ethos-U55 and Ethos-U65 {network_type.name}]"
            f"(#ethos-u55-and-ethos-u65-{network_type.name.lower()}-summary-table)",
        ]

    # Ethos-U85 TFLite
    lines += [
        "- [Ethos-U85 TFLite](#ethos-u85-tflite-summary-table)",
    ]

    # Ethos-U55 and Ethos-U65 TFLite and TOSA
    for network_type in NetworkType:
        if network_type.value in exclude_generation_network_type_value:
            continue

        lines += [
            "",
            f"## Ethos-U55 and Ethos-U65 {network_type.name} Summary Table",
            "",
        ]
        if network_type == NetworkType.TFLite:
            lines += [
                "The table below contains TFLite operators that can be placed on the Ethos-U55 and Ethos-U65.  ",
                "If the constraints are not met, then that operator will be scheduled on the CPU instead.  ",
                "For any other TFLite operator not listed, will be left untouched and scheduled on the CPU.  ",
                "Please check the supported operator list for your chosen runtime for further information.",
                "",
                "| Operator | TFLite Constraints |",
                "| --- | --- |",
            ]
            semantic_checker = TFLiteSemantic()
            supported = TFLiteSupportedOperators()
        elif network_type == NetworkType.TOSA:
            lines += [
                "The table below contains TOSA operators that can be placed on the Ethos-U NPU.  ",
                "Note: There is limited support for compiling a TOSA neural network (EXPERIMENTAL).  ",
                "The related constraints have not yet been populated in the list.",
                "",
                "| Operator | TOSA Constraints |",
                "| --- | --- |",
            ]
            semantic_checker = TosaSemantic()
            supported = TosaSupportedOperators()
        else:
            raise ValueError

        op_constraint_links = []
        op_list = sorted(((op, builtin_operator_name_map[op]) for op in builtin_operator_map), key=lambda x: x[1])
        for op, name in op_list:
            internal_op = builtin_operator_map[op][0]
            if internal_op in TFLiteSupportedOperators.supported_operators:
                links = f"[Generic](#{network_type.name.lower()}-generic-constraints)"
                if (
                    internal_op in supported.specific_constraints
                    or internal_op in semantic_checker.specific_constraints
                ):
                    links += (
                        f", [Specific](#ethos-u55-and-ethos-u65-{network_type.name.lower()}-{name.lower()}-constraints)"
                    )
                    op_constraint_links.append((internal_op, name))
                lines.append(f"| {name} | {links} |")
        # Ethos-U85 TFLite
        lines += [
            """
## Ethos-U85 TFLite Summary Table

The table below contains TFLite operators that can be placed on the Ethos-U85.
If the constraints are not met, then that operator will be scheduled on the CPU instead.
For any other TFLite operator not listed, will be left untouched and scheduled on the CPU.
Please check the supported operator list for your chosen runtime for further information.

| Operator | TFLite Constraints |
| --- | --- |
| ABS | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-abs-constraints) |
| ADD | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-add-constraints) |
| ARG_MAX | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-arg_max-constraints) |
| AVERAGE_POOL_2D | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-average_pool_2d-constraints) |
| BATCH_MATMUL | [Generic](#tflite-generic-constraints) |
| CONCATENATION | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-concatenation-constraints) |
| CONV_2D | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-conv_2d-constraints) |
| DEPTHWISE_CONV_2D | [Generic](#tflite-generic-constraints), \
[Specific](#ethos-u85-tflite-depthwise_conv_2d-constraints) |
| EXP | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-exp-constraints) |
| EXPAND_DIMS | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-expand_dims-constraints) |
| FULLY_CONNECTED | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-fully_connected-constraints) |
| GATHER | [Generic](#tflite-generic-constraints) | [Specific](#ethos-u85-tflite-gather-constraints) |
| HARD_SWISH | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-hard_swish-constraints) |
| LEAKY_RELU | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-leaky_relu-constraints) |
| LOGISTIC | [Generic](#tflite-generic-constraints) |
| MAXIMUM | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-maximum-constraints) |
| MAX_POOL_2D | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-max_pool_2d-constraints) |
| MEAN | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-mean-constraints) |
| MINIMUM | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-minimum-constraints) |
| MUL | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-mul-constraints) |
| PACK | [Generic](#tflite-generic-constraints) |
| PAD | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-pad-constraints) |
| PRELU | [Generic](#tflite-generic-constraints) |
| QUANTIZE | [Generic](#tflite-generic-constraints) |
| RELU | [Generic](#tflite-generic-constraints) |
| RELU6 | [Generic](#tflite-generic-constraints) |
| RELU_N1_TO_1 | [Generic](#tflite-generic-constraints) |
| RESHAPE | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-reshape-constraints) |
| RESIZE_BILINEAR | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-resize_bilinear-constraints) |
| RESIZE_NEAREST_NEIGHBOR | [Generic](#tflite-generic-constraints), \
[Specific](#ethos-u85-tflite-resize_nearest_neighbor-constraints) |
| RSQRT | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-rsqrt-constraints) |
| SCATTER | [Generic](#tflite-generic-constraints) | [Specific](#ethos-u85-tflite-scatter-constraints) |
| SHAPE | [Generic](#tflite-generic-constraints) |
| SLICE | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-slice-constraints) |
| SOFTMAX | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-softmax-constraints) |
| SPLIT | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-split-constraints) |
| SPLIT_V | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-split_v-constraints) |
| SQUARED_DIFFERENCE | [Generic](tflite-generic-constraints), \
[Specific](#ethos-u85-tflite-squared_difference-constraints) |
| SQUEEZE | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-squeeze-constraints) |
| STRIDED_SLICE | [Generic](#tflite-generic-constraints), \
[Specific](#ethos-u85-tflite-strided_slice-constraints) |
| SUB | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-sub-constraints) |
| TANH | [Generic](#tflite-generic-constraints) |
| TRANSPOSE | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-transpose-constraints) |
| TRANSPOSE_CONV | [Generic](#tflite-generic-constraints), [Specific](#ethos-u85-tflite-transpose_conv-constraints) |
| UNPACK | [Generic](#tflite-generic-constraints) |
"""
        ]
        lines += [
            "",
            f"### {network_type.name} Generic Constraints",
            "",
            "This is a list of constraints that most operators must satisfy in order to be scheduled on the NPU.",
            "(Operators excluded from certain constraints are shown in brackets [ ] )\n" "",
        ]
        for constraint in semantic_checker.generic_constraints:
            # Markdown needs two spaces at the end of a line to render it as a separate line
            reason = constraint.__doc__.replace("\n", "  \n")
            exclude_list = TFLiteSemantic.get_generic_constraint_exclude_list().items()
            lines.append(f"- {reason}{_exclude_list_names(constraint, exclude_list)}")
        for constraint in supported.generic_constraints:
            # Markdown needs two spaces at the end of a line to render it as a separate line
            reason = constraint.__doc__.replace("\n", "  \n")
            exclude_list = supported.generic_constraints_exceptions.items()
            lines.append(f"- {reason}{_exclude_list_names(constraint, exclude_list)}")
        lines += ["", "## Ethos-U55 and Ethos-U65 Specific Operator constraints"]
        for op, name in op_constraint_links:
            lines += [
                "",
                f"### Ethos-U55 and Ethos-U65 {network_type.name} {name} Constraints",
                "",
                f"This is a list of constraints that the {name} operator must satisfy in order to be scheduled on the"
                " NPU.",
                "",
            ]
            for constraint in semantic_checker.specific_constraints[op]:
                # Markdown needs two spaces at the end of a line to render it as a separate line
                reason = constraint.__doc__.replace("\n", "  \n")
                lines.append(f"- {reason}")
            for constraint in supported.specific_constraints[op]:
                # Markdown needs two spaces at the end of a line to render it as a separate line
                reason = constraint.__doc__.replace("\n", "  \n")
                lines.append(f"- {reason}")

    # Ethos-U85 TFLite
    lines += [
        """
## Ethos-U85 Specific Operator Constraints

### Ethos-U85 TFLite ABS Constraints

This is a list of constraints that the ABS operator must satisfy in order to be scheduled on the NPU.

- At least one Input's shape must match the OFM's shape
- IFM and OFM data types must match

### Ethos-U85 TFLite ADD Constraints

This is a list of constraints that the ADD operator must satisfy in order to be scheduled on the NPU.

- At least one Input's shape must match the OFM's shape
- Both Input data types must match
- For IFM that are signed, OFM must also be signed
- For IFM that are unsigned, OFM must either be the same type or int32
- Broadcasting is only allowed for rank indices with dimension 1, from either IFM1 or IFM2

### Ethos-U85 TFLite ARG_MAX Constraints

This is a list of constraints that the ARG_MAX operator must satisfy in order to be scheduled on the NPU.

- OFM must be int32 or int64

### Ethos-U85 TFLite AVERAGE_POOL_2D Constraints

This is a list of constraints that the AVERAGE_POOL_2D operator must satisfy in order to be scheduled on the NPU.

- Stride values for both width and height must be integer types
- IFM and OFM data types must match
- Kernel filter values for both width and height must be integer types
- Stride values for both width and height must be in the range [1, 3]
- Kernel filter values for both width and height must be in the range [1, 8]
- VALID padding: Kernel filter height must be in the range [1, 256]
- VALID padding: Product of kernel filter width and height must be in the range [1, 65536]

### Ethos-U85 TFLite CONCATENATION Constraints

This is a list of constraints that the CONCATENATION operator must satisfy in order to be scheduled on the NPU.

- Axis attribute must exist
- Axis attribute must be in the range [0, <ofm_dimensions>)
- All Input dimensionalities must match OFM dimensionality
- All Input dimensions must match OFM dimension in all axes except the one defined by the axis attribute
- The size of the OFM axis must match the sum of all IFM axis defined by the axis attribute

### Ethos-U85 TFLite CONV_2D Constraints

This is a list of constraints that the CONV_2D operator must satisfy in order to be scheduled on the NPU.

- Stride values for both width and height must be integer types
- IFM depth must be a whole multiple of the filter kernel depth
- Dilation factor values for both width and height must be integer types
- Stride values for both width and height must be in the range [1, 3]
- Dilated kernel height must be in the range [1, 64]
- Product of dilated kernel width and height must be in the range [1, 4096]
- Weight tensor must be 8-bit
- Weight tensor must be constant
- The sum of the weights cannot exceed 8323072
- Optional Bias tensor must be of shape: 1D
- Optional Bias tensor must be of type: int32, int64
- Optional Bias tensor values must fit within 40-bits

### Ethos-U85 TFLite DEPTHWISE_CONV_2D Constraints

This is a list of constraints that the DEPTHWISE_CONV_2D operator must satisfy in order to be scheduled on the NPU.

- Stride values for both width and height must be integer types
- Dilation factor values for both width and height must be integer types
- Dilated kernel height must be in the range [1, 64]
- Product of dilated kernel width and height must be in the range [1, 4096]
- Weight tensor must be 8-bit
- Weight tensor must be constant
- The sum of the weights cannot exceed 8323072
- Optional Bias tensor must be of shape: 1D
- Optional Bias tensor must be of type: int32, int64
- Optional Bias tensor values must fit within 40-bits
- Stride values for both width and height must be between 1 and 3
- For depth multipliers > 1, IFM channels must be 1 and OFM channels must be equal to the depth multiplier

### Ethos-U85 TFLite EXP Constraints

This is a list of constraints that the EXP operator must satisfy in order to be scheduled on the NPU.

- At least one Input's shape must match the OFM's shape
- IFM and OFM data types must match
- IFM must be int8 or int16

### Ethos-U85 TFLite EXPAND_DIMS Constraints

This is a list of constraints that the EXPAND_DIMS operator must satisfy in order to be scheduled on the NPU.

- Input and output quantisation must match.
- Input and output number of elements must match.

### Ethos-U85 TFLite FULLY_CONNECTED Constraints

This is a list of constraints that the FULLY_CONNECTED operator must satisfy in order to be scheduled on the NPU.

- The output tensor(s) must have 2D shape
- The IFM and OFM must have the same number of dimensions if keep_num_dims is set to true
- Weight tensor must be 8-bit
- Weight tensor must be constant
- Optional Bias tensor must be of shape: 1D
- Optional Bias tensor must be of type: int32, int64
- Optional Bias tensor values must fit within 40-bits

### Ethos-U85 TFLite GATHER Constraints

This is a list of constraints that the GATHER operator must satisfy in order to be scheduled on the NPU.

- Axis parameter must equal batch dim parameter

### Ethos-U85 TFLite HARD_SWISH Constraints

This is a list of constraints that the HARD_SWISH operator must satisfy in order to be scheduled on the NPU.

- IFM must be int8 or uint8
- IFM and OFM data types must match

### Ethos-U85 TFLite LEAKY_RELU Constraints

This is a list of constraints that the LEAKY_RELU operator must satisfy in order to be scheduled on the NPU.

- At least one Input's shape must match the OFM's shape
- IFM and OFM data types must match

### Ethos-U85 TFLite MAXIMUM Constraints

This is a list of constraints that the MAXIMUM operator must satisfy in order to be scheduled on the NPU.

- At least one Input's shape must match the OFM's shape
- IFM and OFM data types must match
- Both Input quantization parameters must match OFM quantization parameters
- Broadcasting is only allowed for rank indices with dimension 1, from either IFM1 or IFM2

### Ethos-U85 TFLite MAX_POOL_2D Constraints

This is a list of constraints that the MAX_POOL_2D operator must satisfy in order to be scheduled on the NPU.

- Stride values for both width and height must be integer types
- IFM and OFM data types must match
- Kernel filter values for both width and height must be integer types
- Stride values for both width and height must be in the range [1, 3]
- Kernel filter height must be in the range [1, 256]
- Product of kernel filter width and height must be in the range [1, 65536]

### Ethos-U85 TFLite MEAN Constraints

This is a list of constraints that the MEAN operator must satisfy in order to be scheduled on the NPU.

- Input tensor must be at least 2D
- Requirements for axis parameter:  
        When IFM tensor is 2D:  
          - Reduction in both axes is supported.  
        When IFM tensor is 3D or 4D:  
          - Reduction in Batch axis is only supported if batch size is 1.  
          - Reduction in both Height and Width axes is supported.  
          - Reduction in Depth axis is supported if at least one of H,W,C are of size 1.
- Product of reduced axes must be no greater than:  
        - 16777216 for signed 8-bit inputs.  
        - 8388608 for unsigned 8-bit inputs.  
        - 65536 for signed 16-bit inputs.
- If Width axis is reduced its shape must be no greater than 4096.
- If Depth axis is reduced its shape must be no greater than 4096.

### Ethos-U85 TFLite MINIMUM Constraints

This is a list of constraints that the MINIMUM operator must satisfy in order to be scheduled on the NPU.

- At least one Input's shape must match the OFM's shape
- IFM and OFM data types must match
- Both Input quantization parameters must match OFM quantization parameters
- Broadcasting is only allowed for rank indices with dimension 1, from either IFM1 or IFM2

### Ethos-U85 TFLite MIRROR_PAD Constraints

This is a list of constraints that the MIRROR_PAD operator must satisfy in order to be scheduled on the NPU.

- The padding tensor must have the shape [3,2] or [4,2]
- The pad tensor can only pad width and height
- Pad tensor must be of type: int32, int64
- The number of pad values for each direction must not be larger than the ifm size in that dimension

### Ethos-U85 TFLite MUL Constraints

This is a list of constraints that the MUL operator must satisfy in order to be scheduled on the NPU.

- At least one Input's shape must match the OFM's shape
- Both Input data types must match
- For IFM that are signed, OFM must also be signed
- For IFM that are unsigned, OFM must either be the same type or int32
- Broadcasting is only allowed for rank indices with dimension 1, from either IFM1 or IFM2

### Ethos-U85 TFLite PAD Constraints

This is a list of constraints that the PAD operator must satisfy in order to be scheduled on the NPU.

- Number of input tensors must be exactly 2
- The padding tensor must be constant
- Shape of output tensor must equal to size of input tensor plus padding
- The padding tensor must have the shape [3,2] or [4,2]
- The pad tensor can only pad width and height
- Pad tensor must be of type: int32, int64

### Ethos-U85 TFLite RESHAPE Constraints

This is a list of constraints that the RESHAPE operator must satisfy in order to be scheduled on the NPU.

- Input and output quantisation must match.
- Input and output number of elements must match.
- Shape must be constant

### Ethos-U85 TFLite RESIZE_BILINEAR Constraints

This is a list of constraints that the RESIZE_BILINEAR operator must satisfy in order to be scheduled on the NPU.

- The size tensor must match the output tensor shape
- Both align_corners and half_pixel_centers can't be True

### Ethos-U85 TFLite RESIZE_NEAREST_NEIGHBOR Constraints

This is a list of constraints that the RESIZE_NEAREST_NEIGHBOR operator must satisfy in order to be scheduled on the \
NPU.

- The size tensor must match the output tensor shape
- Both align_corners and half_pixel_centers can't be True

### Ethos-U85 TFLite RSQRT Constraints

This is a list of constraints that the RSQRT operator must satisfy in order to be scheduled on the NPU.

- At least one Input's shape must match the OFM's shape
- IFM and OFM data types must match
- IFM must be int8

### Ethos-U85 TFLite SCATTER Constraints

This is a list of constraints that the SCATTER operator must satisfy in order to be scheduled on the NPU.

- Index tensor must be constant
- Index tensor must not contain any duplicate values
- Last dimension of index tensor must be 1
- Shape tensor must be constant

### Ethos-U85 TFLite SLICE Constraints

This is a list of constraints that the SLICE operator must satisfy in order to be scheduled on the NPU.

- Begin and Size Input tensors must be constant

### Ethos-U85 TFLite SOFTMAX Constraints

This is a list of constraints that the SOFTMAX operator must satisfy in order to be scheduled on the NPU.

- IFM and OFM shapes must match
- IFM and OFM data types must match
- Beta value needs to be positive

### Ethos-U85 TFLite SPLIT Constraints

This is a list of constraints that the SPLIT operator must satisfy in order to be scheduled on the NPU.

- Axis value must be in the range [-RANK(IFM) to +RANK(IFM))
- Axis must be divisible by number of splits

### Ethos-U85 TFLite SPLIT_V Constraints

This is a list of constraints that the SPLIT_V operator must satisfy in order to be scheduled on the NPU.

- Only one size is allowed to be inferred

### Ethos-U85 TFLite SQUARED_DIFFERENCE Constraints

This is a list of constraints that the SQUARED_DIFFERENCE operator must satisfy in order to be scheduled on the NPU.

- At least one Input's shape must match the OFM's shape

### Ethos-U85 TFLite SQUEEZE Constraints

This is a list of constraints that the SQUEEZE operator must satisfy in order to be scheduled on the NPU.

- Input and output quantisation must match.
- Input and output number of elements must match.

### Ethos-U85 TFLite STRIDED_SLICE Constraints

This is a list of constraints that the STRIDED_SLICE operator must satisfy in order to be scheduled on the NPU.

- Exactly 4 Input tensors are required
- Begin, End and Stride Input tensors must be constant
- ellipsis_mask must be 0
- new_axis_mask and shrink_axis_mask cannot both be set
- Slice 'end' values must be greater than 'begin' values
- All Strides values must be 1
- Offset attribute must be False

### Ethos-U85 TFLite SUB Constraints

This is a list of constraints that the SUB operator must satisfy in order to be scheduled on the NPU.

- At least one Input's shape must match the OFM's shape
- Both Input data types must match
- For IFM that are signed, OFM must also be signed
- For IFM that are unsigned, OFM must either be the same type or int32
- Broadcasting is only allowed for rank indices with dimension 1, from either IFM1 or IFM2

### Ethos-U85 TFLite TRANSPOSE Constraints

This is a list of constraints that the TRANSPOSE operator must satisfy in order to be scheduled on the NPU.

- Permutation array must be a 1D tensor with RANK(IFM) elements
- Permutation array must have constant values in the range [0, RANK(IFM))

### Ethos-U85 TFLite TRANSPOSE_CONV Constraints

This is a list of constraints that the TRANSPOSE_CONV operator must satisfy in order to be scheduled on the NPU.

- Stride values for both width and height must be integer types
- Dilated kernel height must be in the range [1, 64]
- Product of dilated kernel width and height must be in the range [1, 4096]
- Weight tensor must be 8-bit
- Weight tensor must be constant
- The sum of the weights cannot exceed 8323072
- Optional Bias tensor must be of shape: 1D
- Optional Bias tensor must be of type: int32, int64
- Optional Bias tensor values must fit within 40-bits
- Stride values for width and height must match one of the following criteria:  
        Stride values WxH must be 1x1 or 2x2  
        Stride WxH 2x1 supported if ifm height and kernel height = 1
- SAME padding: OFM dimensions must equal IFM dimensions multiplied by stride
- VALID padding: OFM dimensions must equal IFM dimensions multiplied by stride,  
        minus difference between kernel size and stride
"""
    ]

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
    config = config.rstrip("|") + "\n"

    config += "\n[graph]\n"
    if verbose_graph:
        config += "verbose=true\n"
    if verbose_quantization:
        config += "verbose_quantization=true\n"

    return config


def get_format(in_data: bytes) -> str:
    """Infere format based on input file."""
    ret = "UNDEFINED"
    if len(in_data) < 8:
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


def main(argv: Optional[List[str]] = None) -> int:
    """Run the main entry point."""
    if argv is None:
        argv = sys.argv[1:]

    parser = argparse.ArgumentParser(prog="vela", description="Neural network model compiler for Arm Ethos-U NPUs")
    parser.add_argument("--version", action="version", version=__version__)
    parser.add_argument(
        "--api-version", action="version", version=API_VERSION, help="Displays the version of the external API."
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
        help="Output format (default: %(default)s)",
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
    parser.add_argument("--verbose-packing", action="store_true", help="Enable pass packing debug")
    parser.add_argument("--verbose-tensor-purpose", action="store_true", help="Enable tensor purpose debug")
    parser.add_argument("--verbose-tensor-format", action="store_true", help="Enable tensor format debug")
    parser.add_argument("--verbose-schedule", action="store_true", help="Enable schedule debug")
    parser.add_argument("--verbose-allocation", action="store_true", help="Enable tensor allocation debug")
    parser.add_argument(
        "--verbose-high-level-command-stream", action="store_true", help="Enable high level command stream debug"
    )
    parser.add_argument(
        "--verbose-register-command-stream", action="store_true", help="Enable register command stream debug"
    )
    parser.add_argument("--verbose-operators", action="store_true", help="Enable operator list debug")
    parser.add_argument("--verbose-weights", action="store_true", help="Enable weights information debug")
    parser.add_argument("--verbose-cycle-estimate", action="store_true", help="Enable cycle estimate information debug")
    parser.add_argument("--verbose-performance", action="store_true", help="Enable performance information debug")
    parser.add_argument("--verbose-progress", action="store_true", help="Enable progress information debug")
    parser.add_argument(
        "--show-cpu-operations", action="store_true", help="Show the operations that fall back to the CPU"
    )
    parser.add_argument("--timing", action="store_true", help="Time the compiler doing operations")
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
        action="store_true",
        help="Shows a summary of all the subgraphs and their inputs and outputs",
    )
    parser.add_argument(
        "--max-block-dependency",
        type=int,
        default=architecture_features.ArchitectureFeatures.MAX_BLOCKDEP,
        choices=range(0, architecture_features.ArchitectureFeatures.MAX_BLOCKDEP + 1),
        help=(
            "Set the maximum value that can be used for the block dependency between npu kernel operations"
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
            "Controls the allocation byte alignment of cpu tensors including Ethos-U Custom"
            " operator inputs and outputs (default: %(default)s Bytes)"
        ),
    )
    parser.add_argument(
        "--recursion-limit",
        type=int,
        default=1000,
        help="Set the recursion depth limit, may result in RecursionError if too low (default: %(default)s)",
    )
    parser.add_argument(
        "--hillclimb-max-iterations",
        type=int,
        default=HillClimbAllocator.MAX_ITERATIONS,
        help="Set the maximum number of iterations the Hill Climb tensor allocator will run (default: %(default)s)",
    )

    # debug options
    parser.add_argument("--debug-force-regor", action="store_true", help="Debug: Force the use of the regor")
    parser.add_argument("--disable-chaining", action="store_true", default=False)
    parser.add_argument("--disable-fwd", action="store_true", default=False)
    parser.add_argument("--disable-cascading", action="store_true", default=False)
    parser.add_argument("--disable-buffering", action="store_true", default=False)

    args = parser.parse_args(argv)

    # Generate the supported ops report and exit
    if args.supported_ops_report:
        generate_supported_ops()
        return 0

    if args.list_config_files:
        list_config_files()
        return 0

    if args.network is None:
        parser.error("the following argument is required: NETWORK")

    def _parse_config(config):
        # Make sure the correct separator is used depending on OS
        config = os.path.normpath(config)

        if not config.endswith(".ini"):
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

    # The default behaviour is to use Vela for Ethos-U55/U65 and Regor for Ethos-U85. However, developers can override
    # this by using the --debug-force-regor option
    if arch.is_ethos_u85_system or args.debug_force_regor:
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
        )

        process_regor(
            arch,
            args.network,
            accelerator,
            system_config,
            options,
            args.enable_debug_db,
            args.output_dir,
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
                args.network, args.enable_debug_db, arch, model_reader_options, compiler_options, scheduler_options
            )

        except VelaError as e:
            print(e.data)
            return 1

        if args.show_subgraph_io_summary:
            print_subgraph_io_summary(nng)

    return 0
