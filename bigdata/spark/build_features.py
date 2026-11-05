#!/usr/bin/env python3
"""T05 PySpark feature job: HDFS hourly TSV -> versioned features and comparisons."""

from __future__ import annotations

import argparse
import json
import re
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


DIMENSIONS: tuple[dict[str, str], ...] = (
    {"name": "station_id", "meaning": "带命名空间的分析站点", "availability": "AVAILABLE"},
    {"name": "hour_utc", "meaning": "UTC整点，特征行代表的小时 [t0, t0+1)", "availability": "AVAILABLE"},
    {"name": "origin_time", "meaning": "预测原点 T，等于该小时结束时刻 t0+1h", "availability": "AVAILABLE"},
    {"name": "hour_of_day", "meaning": "0至23时", "availability": "AVAILABLE"},
    {"name": "weekday_monday_0", "meaning": "星期一为0", "availability": "AVAILABLE"},
    {"name": "is_weekend", "meaning": "周末标记", "availability": "AVAILABLE"},
    {"name": "month", "meaning": "UTC月份", "availability": "AVAILABLE"},
    {"name": "day_of_month", "meaning": "UTC月内日期", "availability": "AVAILABLE"},
    {"name": "week_of_year", "meaning": "UTC年内周", "availability": "AVAILABLE"},
    {"name": "energy_kwh", "meaning": "会话按重叠秒数分摊的小时电量", "availability": "AVAILABLE"},
    {"name": "session_count", "meaning": "该小时开始的会话数", "availability": "AVAILABLE"},
    {"name": "coverage_ratio", "meaning": "覆盖秒数除以3600", "availability": "AVAILABLE"},
    {"name": "observation_kind", "meaning": "SESSION_ALLOCATED，非逐小时电表实测", "availability": "AVAILABLE"},
    {"name": "capacity_piles", "meaning": "站级桩容量", "availability": "NOT_COLLECTED"},
    {"name": "occupied_piles", "meaning": "小时占用桩数", "availability": "NOT_COLLECTED"},
    {"name": "amount_yuan", "meaning": "收入金额", "availability": "NOT_COLLECTED"},
)

VERSION_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$")


def validate_version(value: str, label: str) -> str:
    """Reject paths and shell syntax before interpolating identifiers into HDFS paths."""
    if not VERSION_RE.fullmatch(value):
        raise ValueError(f"{label} must match {VERSION_RE.pattern}")
    return value


def target_lead_rows(horizon: int) -> int:
    """Origin T is the END of the feature row hour [t0, t0+1), so horizon h labels [t0+h, t0+h+1): lead h rows.

    Anything measured inside [t0, t0+1) (energy, session_count, coverage) is therefore already known at T,
    which is what forecast_v1 requires ("all features may only use data known before T").
    """
    if horizon < 1 or horizon > 24:
        raise ValueError("horizon must be between 1 and 24")
    return horizon


def yesterday_baseline_lag(horizon: int) -> int:
    """Yesterday's matching hour of target [t0+h, t0+h+1) is t0+h-24, i.e. lag 24-h rows; h=24 is the origin row."""
    target_lead_rows(horizon)
    return 24 - horizon


def available_dimension_count() -> int:
    return sum(item["availability"] == "AVAILABLE" for item in DIMENSIONS)


def _spark_imports() -> dict[str, Any]:
    try:
        from pyspark.sql import SparkSession, Window
        from pyspark.sql import functions as F
        from pyspark.sql.types import (
            DecimalType,
            IntegerType,
            LongType,
            StringType,
            StructField,
            StructType,
        )
    except ImportError as error:
        raise RuntimeError("PySpark is required; run this file with spark-submit") from error
    return {
        "SparkSession": SparkSession,
        "Window": Window,
        "F": F,
        "DecimalType": DecimalType,
        "IntegerType": IntegerType,
        "LongType": LongType,
        "StringType": StringType,
        "StructField": StructField,
        "StructType": StructType,
    }


def build_frames(spark: Any, input_path: str) -> tuple[Any, Any, Any, Any, dict[str, int]]:
    """Build a complete hourly axis, leakage-safe long training rows, and two 2-D reports."""
    api = _spark_imports()
    F, Window = api["F"], api["Window"]
    schema = api["StructType"]([
        api["StructField"]("station_id", api["StringType"](), False),
        api["StructField"]("hour_utc", api["StringType"](), False),
        api["StructField"]("energy_kwh", api["DecimalType"](38, 12), False),
        api["StructField"]("session_count", api["LongType"](), False),
        api["StructField"]("coverage_seconds", api["IntegerType"](), False),
        api["StructField"]("observation_kind", api["StringType"](), False),
    ])
    raw = spark.read.schema(schema).option("sep", "\t").option("header", "false").csv(input_path)
    # 6700 行量级，缓存后省掉后面每次 count 和 join 重读 HDFS 文本。
    parsed = raw.withColumn(
        "hour_start",
        F.to_timestamp("hour_utc", "yyyy-MM-dd'T'HH:mm:ss'Z'"),
    ).persist()
    invalid = parsed.where(
        F.col("station_id").isNull()
        | F.col("hour_start").isNull()
        | F.col("energy_kwh").isNull()
        | (F.col("energy_kwh") < F.lit(0))
        | F.col("coverage_seconds").isNull()
        | (F.col("coverage_seconds") < 0)
        | (F.col("coverage_seconds") > 3600)
    ).count()
    duplicates = (
        parsed.groupBy("station_id", "hour_start").count().where(F.col("count") > 1).count()
    )
    if invalid:
        raise ValueError(f"INVALID_HOURLY_ROWS={invalid}")
    if duplicates:
        raise ValueError(f"DUPLICATE_STATION_HOURS={duplicates}")

    bounds = parsed.groupBy("station_id").agg(
        F.min("hour_start").alias("min_hour"),
        F.max("hour_start").alias("max_hour"),
    )
    axis = bounds.select(
        "station_id",
        F.explode(F.sequence("min_hour", "max_hour", F.expr("INTERVAL 1 HOUR"))).alias("hour_start"),
    )
    observed = parsed.select(
        "station_id",
        "hour_start",
        "energy_kwh",
        "session_count",
        "coverage_seconds",
        "observation_kind",
    )
    completed = (
        axis.join(observed, ["station_id", "hour_start"], "left")
        .withColumn("is_observed", F.col("energy_kwh").isNotNull())
        .withColumn("is_missing", F.col("energy_kwh").isNull())
        .withColumn("session_count", F.coalesce(F.col("session_count"), F.lit(0).cast("long")))
        .withColumn("coverage_seconds", F.coalesce(F.col("coverage_seconds"), F.lit(0)))
        .withColumn("coverage_ratio", F.col("coverage_seconds") / F.lit(3600.0))
        .withColumn("observation_kind", F.coalesce(F.col("observation_kind"), F.lit("MISSING")))
        .withColumn("hour_of_day", F.hour("hour_start"))
        .withColumn("weekday_monday_0", F.pmod(F.dayofweek("hour_start") + F.lit(5), F.lit(7)))
        .withColumn("is_weekend", F.col("weekday_monday_0").isin(5, 6))
        .withColumn("month", F.month("hour_start"))
        .withColumn("day_of_month", F.dayofmonth("hour_start"))
        .withColumn("week_of_year", F.weekofyear("hour_start"))
        .withColumn("origin_time", F.expr("hour_start + INTERVAL 1 HOURS"))
    )
    ordered = Window.partitionBy("station_id").orderBy("hour_start")
    history3 = ordered.rowsBetween(-3, -1)
    history24 = ordered.rowsBetween(-24, -1)
    completed = (
        completed.withColumn("lag1_kwh", F.lag("energy_kwh", 1).over(ordered))
        .withColumn("lag24_kwh", F.lag("energy_kwh", 24).over(ordered))
        .withColumn("rolling_mean_3_kwh", F.avg("energy_kwh").over(history3))
        .withColumn("rolling_mean_24_kwh", F.avg("energy_kwh").over(history24))
        .withColumn("feature_available_at", F.col("origin_time"))
    ).persist()

    feature_columns = [
        "station_id", "hour_start", "origin_time", "feature_available_at", "hour_of_day",
        "weekday_monday_0", "is_weekend", "month", "day_of_month", "week_of_year",
        "session_count", "coverage_ratio", "lag1_kwh", "lag24_kwh", "rolling_mean_3_kwh",
        "rolling_mean_24_kwh", "is_observed", "is_missing", "observation_kind",
    ]
    horizon_frames = []
    for horizon in range(1, 25):
        lead_rows = target_lead_rows(horizon)
        baseline_lag = yesterday_baseline_lag(horizon)
        # h=24 的昨日同小时正好落在原点行本身，此时不需要再回看。
        baseline_yesterday = (
            F.col("energy_kwh") if baseline_lag == 0 else F.lag("energy_kwh", baseline_lag).over(ordered)
        )
        frame = (
            completed.withColumn("horizon_hours", F.lit(horizon))
            .withColumn("target_start", F.expr(f"hour_start + INTERVAL {lead_rows} HOURS"))
            .withColumn("target_end", F.expr(f"hour_start + INTERVAL {lead_rows + 1} HOURS"))
            .withColumn("target_energy_kwh", F.lead("energy_kwh", lead_rows).over(ordered))
            .withColumn("baseline_yesterday_kwh", baseline_yesterday)
            .withColumn("baseline_recent3_kwh", F.col("rolling_mean_3_kwh"))
            .select(*feature_columns, "horizon_hours", "target_start", "target_end",
                    "target_energy_kwh", "baseline_yesterday_kwh", "baseline_recent3_kwh")
            # 原点小时自身必须有观测，否则该样本的特征几乎全为空，只会教模型拟合缺失模式。
            .where(F.col("is_observed") & F.col("target_energy_kwh").isNotNull())
        )
        horizon_frames.append(frame)
    training = horizon_frames[0]
    for frame in horizon_frames[1:]:
        training = training.unionByName(frame)
    training = training.persist()

    observed_only = completed.where(F.col("is_observed"))
    station_hour = observed_only.groupBy("station_id", "hour_of_day").agg(
        F.avg("energy_kwh").alias("avg_energy_kwh"),
        F.count(F.lit(1)).alias("observed_rows"),
    )
    weekday_hour = observed_only.groupBy("weekday_monday_0", "hour_of_day").agg(
        F.avg("energy_kwh").alias("avg_energy_kwh"),
        F.count(F.lit(1)).alias("observed_rows"),
    )
    counts = {
        "inputRows": parsed.count(),
        "stationCount": parsed.select("station_id").distinct().count(),
        "axisRows": completed.count(),
        "observedAxisRows": completed.where(F.col("is_observed")).count(),
        "missingAxisRows": completed.where(F.col("is_missing")).count(),
        "trainingRows": training.count(),
    }
    return completed, training, station_hour, weekday_hour, counts


def _write_bounded_json_lines(frame: Any, path: Path, maximum_rows: int, known_rows: int | None = None) -> int:
    # 行数由调用方传入，避免为了检查预算再触发一遍 24 个窗口的完整 DAG。
    count = known_rows if known_rows is not None else frame.count()
    if count > maximum_rows:
        raise ValueError(
            f"LOCAL_EXPORT_BUDGET_EXCEEDED rows={count} max={maximum_rows}; "
            "raise --max-local-rows only if the driver host really has room for the export"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        for line in frame.toJSON().toLocalIterator():
            stream.write(line)
            stream.write("\n")
    return count


def run_job(args: argparse.Namespace) -> dict[str, Any]:
    api = _spark_imports()
    dataset_version = validate_version(args.dataset_version, "dataset_version")
    run_id = validate_version(args.run_id, "run_id")
    spark = (
        api["SparkSession"].builder.appName(f"member3-features-{dataset_version}-{run_id}")
        .config("spark.sql.session.timeZone", "UTC")
        # 默认 200 个 shuffle 分区对 6700 行只是纯开销；VM 只有 2 vCPU，分区调小可显著缩短窗口作业。
        .config("spark.sql.shuffle.partitions", str(args.shuffle_partitions))
        .getOrCreate()
    )
    try:
        completed, training, station_hour, weekday_hour, counts = build_frames(spark, args.input)
        root = args.output.rstrip("/")
        completed.write.mode("overwrite").parquet(f"{root}/hourly_axis")
        training.write.mode("overwrite").partitionBy("horizon_hours").parquet(f"{root}/training")
        station_hour.write.mode("overwrite").parquet(f"{root}/comparisons/station_hour")
        weekday_hour.write.mode("overwrite").parquet(f"{root}/comparisons/weekday_hour")

        report_dir = Path(args.local_report_dir).resolve()
        report_dir.mkdir(parents=True, exist_ok=True)
        station_json = [json.loads(row) for row in station_hour.orderBy("station_id", "hour_of_day").toJSON().collect()]
        weekday_json = [json.loads(row) for row in weekday_hour.orderBy("weekday_monday_0", "hour_of_day").toJSON().collect()]
        F, Window = api["F"], api["Window"]
        ordered = Window.partitionBy("station_id").orderBy("hour_start")
        forecast_features = completed
        for horizon in range(1, 25):
            baseline_lag = yesterday_baseline_lag(horizon)
            forecast_features = forecast_features.withColumn(
                f"baseline_yesterday_h{horizon:02d}_kwh",
                F.col("energy_kwh") if baseline_lag == 0 else F.lag("energy_kwh", baseline_lag).over(ordered),
            )
        newest = Window.partitionBy("station_id").orderBy(F.col("hour_start").desc())
        latest_features = (
            forecast_features.withColumn("_newest", F.row_number().over(newest))
            .where(F.col("_newest") == 1)
            .drop("_newest")
            .orderBy("station_id")
        )
        latest_json = [json.loads(row) for row in latest_features.toJSON().collect()]
        (report_dir / "station-hour.json").write_text(json.dumps(station_json, ensure_ascii=False, indent=2), encoding="utf-8")
        (report_dir / "weekday-hour.json").write_text(json.dumps(weekday_json, ensure_ascii=False, indent=2), encoding="utf-8")
        (report_dir / "latest-features.json").write_text(json.dumps(latest_json, ensure_ascii=False, indent=2), encoding="utf-8")
        exported = _write_bounded_json_lines(
            training.orderBy("hour_start", "station_id", "horizon_hours"),
            report_dir / "training.jsonl",
            args.max_local_rows,
            known_rows=counts["trainingRows"],
        )
        manifest = {
            "contractVersion": "member3_features_v1",
            "datasetVersion": dataset_version,
            "runId": run_id,
            "dataMode": "REAL_ANONYMIZED",
            "observationKind": "SESSION_ALLOCATED",
            "timezone": "UTC",
            "generatedAt": datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
            "hdfsInput": args.input,
            "hdfsOutput": root,
            "localTrainingRows": exported,
            "localExportBudget": args.max_local_rows,
            "counts": counts,
            "originConvention": "origin T = end of the feature row hour (hour_start + 1h); features only use data known at T",
            "labelRule": "horizon h labels [hour_start+h, hour_start+h+1), i.e. [T+h-1, T+h)",
            "baselineRule": (
                "yesterday_same_hour = lag(24-h) of the origin row (h=24 is the origin row itself); "
                "recent_3h_mean = mean of the three hours before the origin row"
            ),
            "trainingRowFilter": "origin hour observed AND target hour observed",
            "dimensions": list(DIMENSIONS),
            "availableDimensionCount": available_dimension_count(),
            "comparisons": ["station_hour", "weekday_hour"],
            "capacityStatus": "NOT_COLLECTED",
            "amountStatus": "NOT_COLLECTED",
        }
        (report_dir / "feature-manifest.json").write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        return manifest
    finally:
        spark.stop()


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset-version", default="course-v1")
    parser.add_argument("--run-id", default="course-001")
    parser.add_argument("--input", default="/charge_platform/hourly/course-v1/course-001")
    parser.add_argument("--output", default="/charge_platform/features/course-v1/course-001")
    parser.add_argument("--local-report-dir", default="runtime/analytics/stage2/course-v1/course-001")
    parser.add_argument("--max-local-rows", type=int, default=400000)
    parser.add_argument("--shuffle-partitions", type=int, default=8)
    return parser.parse_args(argv)


def main() -> None:
    manifest = run_job(parse_args())
    print(f"DATASET_VERSION={manifest['datasetVersion']}")
    print(f"RUN_ID={manifest['runId']}")
    print(f"INPUT_ROWS={manifest['counts']['inputRows']}")
    print(f"AXIS_ROWS={manifest['counts']['axisRows']}")
    print(f"OBSERVED_AXIS_ROWS={manifest['counts']['observedAxisRows']}")
    print(f"TRAINING_ROWS={manifest['counts']['trainingRows']}")
    print(f"AVAILABLE_DIMENSIONS={manifest['availableDimensionCount']}")
    print("COMPARISONS=station_hour,weekday_hour")
    print("SPARK_FEATURES=PASS")


if __name__ == "__main__":
    main()
