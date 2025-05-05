# SPDX-FileCopyrightText: Copyright 2020-2022, 2024 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-FileCopyrightText: (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
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
# Description:
# Writes out per-pass and summary performance statistics to CSV files.
import csv
import sys

import numpy as np

from .nn_graph import PassPlacement
from .npu_performance import BandwidthDirection
from .npu_performance import PassCycles
from .numeric_util import round_up_to_int
from .operation import Op
from .tensor import MemArea
from .tensor import TensorPurpose


def mem_areas_to_report():
    # Exclude SHRAM, as the SHRAM performance numbers only cover LUT usage
    return [area for area in MemArea.all() if area != MemArea.Shram]


def write_summary_metrics_csv_common(
    summary_filename,
    arch,
    name,
    total_original_weights,
    total_npu_encoded_weights,
    n_passes,
    n_cascaded_passes,
    cycles,
    bandwidths,
    memory_used,
    batch_size,
    macs,
):
    with open(summary_filename, "w") as f:
        writer = csv.writer(f)

        mem_areas = mem_areas_to_report()

        labels = [
            "experiment",
            "network",
        ]

        labels += (
            ["accelerator_configuration", "system_config", "memory_mode", "core_clock", "arena_cache_size"]
            + [area.identifier_name() + "_bandwidth" for area in mem_areas]
            + ["weights_storage_area", "feature_map_storage_area"]
        )

        labels += [
            "inferences_per_second",
            "batch_size",
            "inference_time",
            "passes_before_fusing",
            "passes_after_fusing",
        ]
        labels += [area.identifier_name() + "_memory_used" for area in mem_areas]
        labels += ["total_original_weights"]
        labels += ["total_npu_encoded_weights"]

        for mem_area in mem_areas:
            labels += [
                mem_area.identifier_name() + "_feature_map_read_bytes",
                mem_area.identifier_name() + "_feature_map_write_bytes",
                mem_area.identifier_name() + "_weight_read_bytes",
                mem_area.identifier_name() + "_weight_write_bytes",
                mem_area.identifier_name() + "_total_bytes",
            ]

        labels += ["nn_macs", "nn_tops"]

        labels += ["cycles_" + kind.identifier_name() for kind in PassCycles.all()]

        writer.writerow(labels)

        data_items = [
            "default",
            name,
        ]

        if arch:
            data_items += (
                [
                    arch.accelerator_config.name,
                    arch.system_config,
                    arch.memory_mode,
                    arch.core_clock,
                    arch.arena_cache_size / 1024,
                ]
                + [arch.memory_bandwidths_per_second[mem_area] / 1024.0 / 1024 / 1024 for mem_area in mem_areas]
                + [
                    arch.tensor_storage_mem_area[TensorPurpose.Weights].display_name(),
                    arch.tensor_storage_mem_area[TensorPurpose.FeatureMap].display_name(),
                ]
            )

        midpoint_inference_time = cycles[PassCycles.Total] / arch.core_clock
        if midpoint_inference_time > 0:
            midpoint_fps = 1 / midpoint_inference_time
        else:
            midpoint_fps = np.nan

        data_items += [midpoint_fps, batch_size, midpoint_inference_time, n_passes, n_cascaded_passes]
        data_items += [memory_used.get(mem_area, 0) / 1024.0 for mem_area in mem_areas]
        data_items += [total_original_weights]
        data_items += [total_npu_encoded_weights]

        for mem_area in mem_areas:
            bws = bandwidths[mem_area]
            total_bw = np.sum(bws)
            weight_bws = bws[TensorPurpose.Weights]
            fm_bws = bws[TensorPurpose.FeatureMap]
            data_items += [
                fm_bws[BandwidthDirection.Read],
                fm_bws[BandwidthDirection.Write],
                weight_bws[BandwidthDirection.Read],
                weight_bws[BandwidthDirection.Write],
                total_bw,
            ]

        data_items += [
            macs,
            macs * 2 * midpoint_fps / 1e12,
        ]

        data_items += [cycles[kind] for kind in PassCycles.all()]

        writer.writerow(data_items)


def write_summary_metrics_csv(nng, summary_filename, arch):
    n_passes = sum(len(sg.passes) for sg in nng.subgraphs)
    n_cascaded_passes = sum(len(sg.cascaded_passes) for sg in nng.subgraphs)
    write_summary_metrics_csv_common(
        summary_filename,
        arch,
        nng.name,
        nng.total_original_weights,
        nng.total_npu_encoded_weights,
        n_passes,
        n_cascaded_passes,
        nng.cycles,
        nng.bandwidths,
        nng.memory_used,
        nng.batch_size,
        nng.macs,
    )


def write_pass_metrics_csv(nng, pass_filename):

    with open(pass_filename, "w") as f:
        writer = csv.writer(f)

        purpose_list = (
            ("total", (TensorPurpose.Weights, TensorPurpose.FeatureMap)),
            ("weights", (TensorPurpose.Weights,)),
            ("feature_map", (TensorPurpose.FeatureMap,)),
        )

        direction_list = (
            ("total", (BandwidthDirection.Read, BandwidthDirection.Write)),
            ("read", (BandwidthDirection.Read,)),
            ("write", (BandwidthDirection.Write,)),
        )
        bandwidth_names = []
        bandwidth_indices = []
        for mem_area in mem_areas_to_report():
            for purpose, purpose_candidates in purpose_list:
                for direction, direction_candidates in direction_list:
                    label = "bytes_{}_{}_{}".format(mem_area.identifier_name(), purpose, direction)
                    bandwidth_names.append(label)
                    bandwidth_indices.append((mem_area, purpose_candidates, direction_candidates))

        all_cycles = (
            PassCycles.Total,
            PassCycles.Npu,
            PassCycles.SramAccess,
            PassCycles.DramAccess,
            PassCycles.OnChipFlashAccess,
            PassCycles.OffChipFlashAccess,
        )
        writer.writerow(
            [
                "name",
                "operators",
                "placement",
                "streaming_strategy",
                "block_config_height",
                "block_config_width",
                "block_config_input_channels",
                "block_config_output_channels",
            ]
            + ["cycles_" + v.identifier_name() for v in all_cycles]
            + ["nn_macs"]
            + bandwidth_names
            + ["sram_used"]
        )

        def write_subgraph(sg):
            for cps in sg.cascaded_passes:
                if cps.placement == PassPlacement.StartupInit:
                    continue  # skip the dummy init pass

                for ps in cps.passes:
                    if len(ps.ops) == 1 and ps.ops[0].type == Op.CustomNpuOp:
                        # just treat this as a call, unroll it
                        write_subgraph(ps.ops[0].attrs["subgraph"])
                        continue
                    stats = [ps.name, " ".join(op.type.name for op in ps.ops)]
                    stats += [ps.placement.name]
                    stats += [cps.strategy.name]
                    stats += list(ps.block_config)
                    stats += [round_up_to_int(ps.cycles[v]) for v in all_cycles]
                    stats += [round_up_to_int(ps.macs)]
                    for indices in bandwidth_indices:
                        res = 0
                        i = indices[0]
                        for j in indices[1]:
                            for k in indices[2]:
                                res += round_up_to_int(ps.bandwidths[i, j, k])
                        stats.append(res)
                    try:
                        stats += [ps.sram_used]
                    except AttributeError:
                        stats += [0]

                    writer.writerow(stats)

        write_subgraph(nng.get_root_subgraph())


def print_performance_metrics_common(
    arch,
    name,
    cycles,
    macs,
    bandwidths,
    batch_size,
    memory_used,
    cpu_operations=None,
    npu_operations=None,
    show_cpu_operations=False,
    weights_data=None,
    verbose_cycle_estimate=False,
    f=sys.stdout,
):

    orig_mem_areas_labels = [(v, v.display_name()) for v in mem_areas_to_report()]

    inference_time = cycles[-1] / arch.core_clock
    if inference_time > 0:
        inferences_per_second = 1 / inference_time
    else:
        inferences_per_second = np.nan

    mem_area_labels = [
        (mem_area, label) for mem_area, label in orig_mem_areas_labels if np.sum(bandwidths[mem_area]) > 0
    ]

    print("", file=f)
    if name:
        print(f"Network summary for {name}", file=f)
    print(f"Accelerator configuration        {arch.accelerator_config.name:>20}", file=f)
    print(f"System configuration             {arch.system_config:>20}", file=f)
    print(f"Memory mode                      {arch.memory_mode:>20}", file=f)
    print(f"Accelerator clock                        {int(arch.core_clock / 1e6):12d} MHz", file=f)
    for mem_area, label in mem_area_labels:
        label += " bandwidth"
        bandwidth = arch.memory_bandwidths_per_second[mem_area] / 1024.0 / 1024 / 1024
        print(
            f"Design peak {label:25}    {bandwidth:12.2f} GB/s",
            file=f,
        )
    print(file=f)
    for mem_area, label in mem_area_labels:
        if mem_area not in memory_used:
            continue

        aug_label = label + " used"

        print(f"Total {aug_label:25}          {memory_used[mem_area] / 1024.0:12.2f} KiB", file=f)

    print(file=f)

    if cpu_operations is None:
        cpu_operations = []
    if npu_operations is None:
        npu_operations = []

    n_cpu_operations = len(cpu_operations)
    n_npu_operations = len(npu_operations)
    n_total_operations = max(n_cpu_operations + n_npu_operations, 1)  # avoid potential divide by zero

    for str_ops_type, n_ops, ops in (
        ("CPU", n_cpu_operations, cpu_operations),
        ("NPU", n_npu_operations, npu_operations),
    ):
        print(f"{str_ops_type} operators = {n_ops:d} ({n_ops / n_total_operations:4.1%})", file=f)
        if show_cpu_operations:
            for op in ops:
                print(f"   {str_ops_type}: {op}")

    print("", file=f)

    for mem_area, label in mem_area_labels:
        bws = bandwidths[mem_area]
        total_bw = np.sum(bws)
        weight_bws = bws[TensorPurpose.Weights]
        fm_bws = bws[TensorPurpose.FeatureMap]
        aug_label = label + " bandwidth"
        print(
            f"Average {aug_label:25}        {total_bw * inferences_per_second / 1024.0 / 1024.0 / 1000.0:12.2f} GB/s",
            file=f,
        )
        print(
            f"Input   {aug_label:25}        "
            f"{np.sum(fm_bws[BandwidthDirection.Read]) / 1024.0 / 1024.0:12.2f} MB/batch",
            file=f,
        )
        print(f"Weight  {aug_label:25}        {np.sum(weight_bws) / 1024.0 / 1024.0:12.2f} MB/batch", file=f)
        print(
            f"Output  {aug_label:25}        "
            f"{np.sum(fm_bws[BandwidthDirection.Write]) / 1024.0 / 1024.0:12.2f} MB/batch",
            file=f,
        )
        print(f"Total   {aug_label:25}        {total_bw / 1024.0 / 1024.0:12.2f} MB/batch", file=f)
        print(
            f"Total   {aug_label:25} per input "
            f"{total_bw / 1024.0 / 1024.0 / batch_size:9.2f} MB/inference (batch size {batch_size:d})",
            file=f,
        )
        print(file=f)

    if weights_data:
        print(f"Original Weights Size                    {weights_data['original'] / 1024.0:12.2f} KiB", file=f)
        print(f"NPU Encoded Weights Size                 {weights_data['npu_encoded'] / 1024.0:12.2f} KiB", file=f)
        print(file=f)

    print(
        f"Neural network macs                      {int(macs):12d} MACs/batch",
        file=f,
    )
    if verbose_cycle_estimate:
        print(file=f)
        print("Info: The numbers below are internal compiler estimates.", file=f)
        print("For performance numbers the compiled network should be run on an FVP Model or FPGA.", file=f)
        print(file=f)

        print(
            f"Network Tops/s                           {macs * 2 * inferences_per_second / 1e12:12.2f} Tops/s",
            file=f,
        )
        print(file=f)

        for kind in PassCycles.all():
            aug_label = kind.display_name() + " cycles"
            cyc = cycles[kind]
            print(f"{aug_label:30}           {int(cyc):12d} cycles/batch", file=f)
        print(file=f)

        print(
            f"Batch Inference time              {inference_time * 1000:7.2f} ms,"
            f" {inferences_per_second:7.2f} inferences/s (batch size {batch_size:d})",
            file=f,
        )
    print(file=f)


def print_performance_metrics(
    nng, arch, show_cpu_operations=False, verbose_weights=False, verbose_cycle_estimate=False, f=sys.stdout
):
    cpu_operations = []
    npu_operations = []
    ir_only_ops = (
        Op.Const,
        Op.Placeholder,
        Op.CustomNpuOp,
        Op.SubgraphInput,
    )

    def format_tens_list(lst):
        return " ".join(str(list(tens.shape)) for tens in lst if tens is not None)

    for sg in nng.subgraphs:
        if sg.placement == PassPlacement.Cpu:
            for op in sg.get_all_ops_from_passes():
                if op.type not in ir_only_ops:
                    cpu_operations.append(
                        f"{op.type} = {op.name} "
                        f"(inputs {format_tens_list(op.inputs)}, outputs {format_tens_list(op.outputs)})"
                    )
        elif sg.placement == PassPlacement.Npu:
            for op in sg.get_all_ops_from_passes():
                if op.type not in ir_only_ops:
                    npu_operations.append(
                        f"{op.type} = {op.name} "
                        f"(inputs {format_tens_list(op.inputs)}, outputs {format_tens_list(op.outputs)})"
                    )

    weights_data = (
        {"original": nng.total_original_weights, "npu_encoded": nng.total_npu_encoded_weights}
        if verbose_weights
        else None
    )

    return print_performance_metrics_common(
        arch,
        nng.name,
        nng.cycles,
        nng.macs,
        nng.bandwidths,
        nng.batch_size,
        nng.memory_used,
        cpu_operations,
        npu_operations,
        show_cpu_operations,
        weights_data,
        verbose_cycle_estimate,
        f,
    )


def regor_operations_from_database(opt_database):
    def find_in_header(names, header):
        idx = []
        for name in names:
            if name not in header:
                idx.append(-1)
            idx.append(header.index(name))
        return idx

    # set of optimised_ids that ended up on NPU
    npu_optimised_ids = set()
    # maps src-id to ofm-shape
    ofm_shapes = dict()
    cpu_operations = []
    npu_operations = []

    # prerequisite checks
    required_tables = ["source", "optimised", "perf", "queue"]
    for table in required_tables:
        if table not in opt_database.tables:
            print("Could not extract CPU operations:")
            print("Table: {} was not in opt_database".format(table))
            return cpu_operations, npu_operations

    # build ofm_shapes from source table
    src = opt_database.tables["source"]
    fields = ["id", "ofm_w", "ofm_h", "ofm_d"]
    ids = find_in_header(fields, src.header)
    if any([x < 0 for x in ids]):
        print("Could not extract CPU operations:")
        print("Could not find all necessary fields in source database")
        return cpu_operations, npu_operations

    id_idx, w_idx, h_idx, d_idx = ids
    for entry in src.data:
        src_id = entry[id_idx]
        w = int(entry[w_idx])
        h = int(entry[h_idx])
        d = int(entry[d_idx])
        ofm_shapes[src_id] = [1, w, h, d]

    # build npu_optimised_ids from queue table
    qt = opt_database.tables["queue"]
    id_idx = find_in_header(["optimised_id"], qt.header)[0]
    if id_idx == -1:
        print("Could not extract CPU operations:")
        print("optimised_id was not found in queue table")
        return cpu_operations, npu_operations
    for entry in qt.data:
        npu_optimised_ids.add(entry[id_idx])

    # build cpu/npu operations from perf-table
    perf = opt_database.tables["perf"]
    fields = ["optimised_id", "source_id", "name", "operator"]
    ids = find_in_header(fields, perf.header)
    if any([x < 0 for x in ids]):
        print("Could not extract CPU operations:")
        print("Could not find all necessary fields in perf database")
    opt_idx, src_idx, name_idx, operator_idx = ids
    for entry in perf.data:
        opt_id = entry[opt_idx]
        src_id = entry[src_idx]
        name = entry[name_idx]
        operator = entry[operator_idx]
        ofm_shape = ofm_shapes[src_id]
        # TODO add ifm shapes
        op_desc = f"{operator} = {name} (outputs {ofm_shape})"
        if opt_id in npu_optimised_ids:
            npu_operations.append(op_desc)
        else:
            cpu_operations.append(op_desc)
    return cpu_operations, npu_operations


def print_regor_performance_metrics(
    arch,
    report,
    model_name,
    csv_filename,
    opt_database,
    verbose_weights=False,
    verbose_cycle_estimate=False,
    show_cpu_operations=False,
):
    # Map from Regor memory names to Vela MemArea
    memory_mapping = {
        "sram": MemArea.Sram,
        "dram": MemArea.Dram,
        "onchipflash": MemArea.OnChipFlash,
        "offchipflash": MemArea.OffChipFlash,
        "lutram": MemArea.Shram,
        "shram": MemArea.Shram,
    }

    # Map from Regor memory names to Vela PassCycles
    cycles_mapping = {
        "sram": PassCycles.SramAccess,
        "dram": PassCycles.DramAccess,
        "onchipflash": PassCycles.OnChipFlashAccess,
        "offchipflash": PassCycles.OffChipFlashAccess,
    }

    # Map from Regor access_type to Vela TensorPurpose
    purpose_mapping = {
        "lut": TensorPurpose.LUT,
        "featuremap": TensorPurpose.FeatureMap,
        "scale": TensorPurpose.FSBias,
        "weights": TensorPurpose.Weights,
    }

    cycles = [0] * PassCycles.Size
    bandwidths = [[[0] * BandwidthDirection.Size for i in range(TensorPurpose.Size)] for j in range(MemArea.Size)]
    memory_used = {i: 0 for i in range(MemArea.Size)}
    for mem_name, memory in report.memories.items():
        for _, a in memory.accesses.items():
            mem_name_lower = str(mem_name).lower()

            # skip shram/lutram in performance report
            if mem_name_lower in ["lutram", "shram"]:
                continue

            mem_area = memory_mapping.get(mem_name_lower, MemArea.Unknown)
            purpose = purpose_mapping.get(str(a.accessType).lower(), TensorPurpose.Unknown)

            if mem_name_lower in cycles_mapping:
                cycles_idx = cycles_mapping[mem_name_lower]
                cycles[cycles_idx] += a.accessCycles

            bandwidths[mem_area][purpose][BandwidthDirection.Read] = a.bytesRead
            bandwidths[mem_area][purpose][BandwidthDirection.Write] = a.bytesWritten

            memory_used[mem_area] = memory.peakUsage

    cycles[PassCycles.Npu] = report.npuCycles
    cycles[PassCycles.Total] = report.totalCycles

    batch_size = 1

    npu_operations = list()
    cpu_operations = list()

    if opt_database:
        if show_cpu_operations:
            cpu_operations, npu_operations = regor_operations_from_database(opt_database)

    if not len(cpu_operations) and not len(npu_operations):
        cpu_operations = ["N/A"] * report.cpuOps
        npu_operations = ["N/A"] * report.npuOps

    total_original_weights = report.originalWeights
    total_npu_encoded_weights = report.encodedWeights

    weights_data = (
        {"original": total_original_weights, "npu_encoded": total_npu_encoded_weights} if verbose_weights else None
    )
    f = sys.stdout

    print_performance_metrics_common(
        arch,
        model_name,
        cycles,
        report.macCount,
        bandwidths,
        batch_size,
        memory_used,
        cpu_operations,
        npu_operations,
        show_cpu_operations,
        weights_data,
        verbose_cycle_estimate,
        f,
    )

    n_passes = report.cpuOps + report.npuOps
    n_passes_after_cascading = n_passes - report.cascadedOps + report.cascades

    write_summary_metrics_csv_common(
        csv_filename,
        arch,
        model_name,
        total_original_weights,
        total_npu_encoded_weights,
        n_passes,
        n_passes_after_cascading,
        cycles,
        bandwidths,
        memory_used,
        batch_size,
        report.macCount,
    )


def postprocess_regor_performance_database(arch, opt_database, performance_report):
    if "perf" not in opt_database.tables:
        return None, None

    def find_in_header(name, header):
        if name not in header:
            return -1
        return header.index(name)

    def _percentage(x, y):
        if y == 0:
            return 100
        return (x * 100) / y

    # Set of operations in the optimised-table that ended up as commands
    npu_operators = set()
    if "queue" in opt_database.tables:
        queue_table = opt_database.tables["queue"]
        queue_header = queue_table.header
        queue_rows = queue_table.data
        opt_idx = find_in_header("optimised_id", queue_header)
        if opt_idx != -1:
            for row in queue_rows:
                npu_operators.add(int(row[opt_idx]))

    perf_table = opt_database.tables["perf"]
    perf_header = perf_table.header
    perf_rows = perf_table.data

    # populate id -> name map for source operations
    source_names = {}

    if "source" in opt_database.tables:
        source_table = opt_database.tables["source"]
        source_header = source_table.header
        source_rows = source_table.data
        op_name_idx = find_in_header("name", source_header)
        op_id_idx = find_in_header("id", source_header)
        if op_name_idx != -1 and op_id_idx != -1:
            for row in source_rows:
                op_id = row[op_id_idx]
                op_name = row[op_name_idx]
                source_names[op_id] = op_name

    # maps vela-columns to columns in the regor performance-database
    col_map = {
        "SourceId": find_in_header("source_id", perf_header),
        "OptId": find_in_header("optimised_id", perf_header),
        "NNG Operator": find_in_header("operator", perf_header),
        "Staging Usage": find_in_header("staging_usage", perf_header),
        "Op Cycles": find_in_header("op_cycles", perf_header),
        "NPU": find_in_header("npu_cycles", perf_header),
        "MAC Count": find_in_header("mac_count", perf_header),
        "SRAM AC": find_in_header("Sram_ac", perf_header),
        "DRAM AC": find_in_header("Dram_ac", perf_header),
        "OnFlash AC": find_in_header("OnChipFlash_ac", perf_header),
        "OffFlash AC": find_in_header("OffChipFlash_ac", perf_header),
        "Name": find_in_header("name", perf_header),
    }

    # generate new header and data that aligns with vela format
    new_header = [
        "Original Operator",
        "NNG Operator",
        "Target",
        "Staging Usage",
        "Peak% (Staging)",
        "Op Cycles",
        "Network% (cycles)",
        "NPU",
        "SRAM AC",
        "DRAM AC",
        "OnFlash AC",
        "OffFlash AC",
        "MAC Count",
        "Network% (MAC)",
        "Util% (MAC)",
        "Name",
    ]
    new_data = []
    for row in perf_rows:
        new_row = []
        for col in new_header:
            if col in col_map:
                idx = col_map[col]
                if idx == -1:
                    new_row.append("0")
                else:
                    new_row.append(row[idx])
            # post-processing for things not contained in regors performance-database
            else:
                if col == "Target":
                    opt_id_idx = col_map["OptId"]
                    if opt_id_idx != -1 and len(npu_operators):
                        opt_id = row[opt_id_idx]
                        if int(opt_id) in npu_operators:
                            new_row.append("NPU")
                        else:
                            new_row.append("CPU")
                    else:
                        new_row.append("N/A")
                elif col == "Original Operator":
                    source_id_idx = col_map["SourceId"]
                    if source_id_idx != -1:
                        source_op_id = row[source_id_idx]
                        source_name = source_names.get(source_op_id, "N/A")
                        new_row.append(source_name)
                    else:
                        new_row.append("N/A")
                elif col == "Peak% (Staging)":
                    staging_idx = col_map["Staging Usage"]
                    if staging_idx != -1:
                        staging_usage = int(row[staging_idx])
                        staging_network = int(
                            performance_report.memories[performance_report.stagingMemoryArea].peakUsage
                        )
                        staging_percent = _percentage(staging_usage, staging_network)
                        new_row.append(str(staging_percent))
                    else:
                        new_row.append("N/A")
                elif col == "Network% (cycles)":
                    cycles_idx = col_map["Op Cycles"]
                    if cycles_idx != -1:
                        cycles = int(row[cycles_idx])
                        cycles_network = int(performance_report.totalCycles)
                        cycles_percent = _percentage(cycles, cycles_network)
                        new_row.append(str(cycles_percent))
                    else:
                        new_row.append("N/A")
                elif col == "Network% (MAC)":
                    mac_idx = col_map["MAC Count"]
                    if mac_idx != -1:
                        macs = int(row[mac_idx])
                        macs_network = int(performance_report.macCount)
                        macs_percent = _percentage(macs, macs_network)
                        new_row.append(str(macs_percent))
                    else:
                        new_row.append("100")
                elif col == "Util% (MAC)":
                    cycles_idx = col_map["Op Cycles"]
                    mac_idx = col_map["MAC Count"]
                    if cycles_idx != -1 and mac_idx != -1:
                        cycles = int(row[cycles_idx])
                        macs = int(row[mac_idx])
                        max_macs = cycles * arch.num_macs_per_cycle * arch.ncores
                        util = _percentage(macs, max_macs)
                        new_row.append(str(util))
                    else:
                        new_row.append("N/A")
                else:
                    new_row.append("N/A")

        new_data.append(new_row)
    return new_header, new_data


def write_regor_db(opt_database, output_basename):
    out = "<?xml version='1.0' encoding='UTF-8'?>\n"
    out += f'<debug source="{output_basename}">\n'
    for name, table in opt_database.tables.items():
        out += f'<table name="{name}">\n'
        out += "<![CDATA[\n"
        out += ",".join(f'"{col}"' for col in table.header) + "\n"
        for row in table.data:
            for i in range(len(row)):
                if not row[i].isnumeric():
                    row[i] = f'"{row[i]}"'
            out += ",".join(f"{val}" for val in row) + "\n"
        out += "]]>\n"
        out += "</table>\n"
    out += "</debug>\n"

    debug_filename = output_basename + "_debug.xml"

    with open(debug_filename, "w", encoding="utf-8") as file:
        file.write(out)


def write_regor_perlayer_performance_csv(arch, opt_database, performance_report, output_basename):
    if "perf" not in opt_database.tables:
        return

    perf_header, perf_rows = postprocess_regor_performance_database(arch, opt_database, performance_report)

    csv_name = f"{output_basename}_per-layer.csv"
    with open(csv_name, "w") as csv_file:
        csv_file.write(",".join(perf_header))
        csv_file.write("\n")
        for row in perf_rows:
            csv_file.write(",".join(row))
            csv_file.write("\n")


def print_regor_perlayer_performance(arch, opt_database, performance_report, output_basename):
    if "perf" not in opt_database.tables:
        return

    perf_header, perf_rows = postprocess_regor_performance_database(arch, opt_database, performance_report)

    # header -> (align, width, precision)
    header = {
        "Original Operator": ("<", 20, -1),
        "NNG Operator": ("<", 20, -1),
        "Target": ("<", 6, -1),
        "Staging Usage": (">", 13, 0.0),
        "Peak% (Staging)": (">", 16, 0.2),
        "Op Cycles": (">", 10, 0.0),
        "Network% (cycles)": (">", 17, 0.2),
        "NPU": (">", 10, 0.0),
        "SRAM AC": (">", 10, 0.0),
        "DRAM AC": (">", 10, 0.0),
        "OnFlash AC": (">", 10, 0.0),
        "OffFlash AC": (">", 11, 0.0),
        "MAC Count": (">", 10, 0.0),
        "Network% (MAC)": (">", 14, 0.2),
        "Util% (MAC)": (">", 12, 0.2),
        "Name": ("<", 20, -1),
    }

    print(f"\n{str('#') * 80}\nPerformance for NPU Grap {output_basename}")

    line = ""
    line2 = ""
    for col_name in header:
        align, width, _ = header[col_name]
        line_data = f"{col_name:{align}{width}}"
        line += line_data + " "
        line2 += "-" * len(line_data) + " "
    print(line)
    print(line2)

    for op_data in perf_rows:
        line = ""
        for table_idx in range(len(perf_header)):
            h = perf_header[table_idx]
            align, width, precision = header[h]
            if precision == -1:
                w = str(width)
            else:
                w = str(width + precision) + "f"
            item = op_data[table_idx]
            if precision != -1:
                item = float(item)
            line += f"{item:{align}{w}}" + " "
        print(line)
