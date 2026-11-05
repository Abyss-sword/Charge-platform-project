package org.group29.charge;

import java.math.BigDecimal;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * staging 小时片段的解析与聚合纯函数（T04）。
 *
 * 输入固定 6 列 TSV：站点ID、UTC小时、未舍入电量、会话开始计数、覆盖秒数、观测类型。
 * 聚合规则：电量用 BigDecimal 累加（金额电量禁用 Double）；会话数求和（跨小时会话只在
 * 起始小时计一次，由上游保证）；覆盖秒数取最大值（输入约定同键一行、且为每站区间并集，
 * 同键多行时取 max 防止超过 3600）；观测类型按 实测>会话估计>公开聚合 取优。
 * 无效行抛 IllegalArgumentException，由 Mapper 计数拒绝、驱动判失败，禁止静默丢弃。
 */
public final class HourlyAggregator {

    /** 拒绝原因常量：JUnit 与 Mapper 共用同一张码表。 */
    public static final String ERR_COLUMNS = "BAD_COLUMN_COUNT";
    public static final String ERR_ENERGY = "BAD_ENERGY";
    public static final String ERR_SESSIONS = "BAD_SESSION_COUNT";
    public static final String ERR_COVERAGE = "BAD_COVERAGE";
    public static final String ERR_KIND = "BAD_OBSERVATION_KIND";
    public static final String ERR_STATION = "BAD_STATION_ID";

    private static final String KIND_METER = "METER_INTERVAL";
    private static final String KIND_SESSION = "SESSION_ALLOCATED";
    private static final String KIND_PUBLIC = "PUBLIC_HOURLY";

    private HourlyAggregator() {
    }

    /** 单行解析：6 列、无空站点/小时、电量非负十进制、会话数与覆盖为合法整数。 */
    public static String[] parse(String line) {
        if (line == null) {
            throw new IllegalArgumentException(ERR_COLUMNS);
        }
        String[] cols = line.split("\t", -1); // -1：保留尾部空列，列数必须精确。
        if (cols.length != 6) {
            throw new IllegalArgumentException(ERR_COLUMNS);
        }
        String station = cols[0];
        String hour = cols[1];
        if (station.isEmpty() || station.contains("\n") || station.contains("\r")) {
            throw new IllegalArgumentException(ERR_STATION); // 站点ID禁止空与换行（契约约定禁止制表，制表会改变列数已被挡）。
        }
        if (hour.isEmpty() || hour.length() != 20 || !hour.endsWith("Z")) {
            throw new IllegalArgumentException(ERR_COLUMNS); // 小时格式 YYYY-MM-DDTHH:00:00Z 固定 20 字符。
        }
        BigDecimal energy = parseEnergy(cols[2]);
        if (energy.signum() < 0) {
            throw new IllegalArgumentException(ERR_ENERGY); // staging 本应干净：负电量判失败不修复。
        }
        if (!cols[3].matches("[0-9]+")) {
            throw new IllegalArgumentException(ERR_SESSIONS);
        }
        long coverage;
        try {
            coverage = Long.parseLong(cols[4]);
        } catch (NumberFormatException error) {
            throw new IllegalArgumentException(ERR_COVERAGE);
        }
        if (coverage < 0 || coverage > 3600) {
            throw new IllegalArgumentException(ERR_COVERAGE);
        }
        String kind = cols[5];
        if (!KIND_METER.equals(kind) && !KIND_SESSION.equals(kind) && !KIND_PUBLIC.equals(kind)) {
            throw new IllegalArgumentException(ERR_KIND);
        }
        return cols;
    }

    /** 电量文本 → BigDecimal：只接受 [0-9]+(.[0-9]+)?，拒绝科学计数法/空白/负号。 */
    public static BigDecimal parseEnergy(String text) {
        if (text == null || !text.matches("[0-9]+(\\.[0-9]+)?")) {
            throw new IllegalArgumentException(ERR_ENERGY);
        }
        return new BigDecimal(text);
    }

    /**
     * 同键聚合：values 为输入行第 3–6 列（电量、会话数、覆盖秒、类型）。
     * 返回输出行第 3–6 列文本；乱序输入与有序输入结果一致（BigDecimal 加法满足交换律）。
     */
    public static String[] aggregate(Iterable<String[]> valueColumns) {
        BigDecimal energy = BigDecimal.ZERO; // 电量精确累加，不丢精度。
        long sessions = 0L; // 会话开始计数求和。
        long coverage = 0L; // 同键取最大（输入契约同键一行；防御多行时不虚增覆盖）。
        int bestKind = -1; // 类型优先级：实测 3 > 会话估计 2 > 公开 1。
        String kind = KIND_PUBLIC; // 默认最低。
        for (String[] cols : valueColumns) { // 乱序遍历也正确。
            energy = energy.add(parseEnergy(cols[0]));
            sessions += Long.parseLong(cols[1]);
            long current = Long.parseLong(cols[2]);
            if (current > coverage) {
                coverage = current;
            }
            int rank = kindRank(cols[3]);
            if (rank > bestKind) { // 并列时保留先到者，输出稳定。
                bestKind = rank;
                kind = cols[3];
            }
        }
        return new String[] {plain(energy), Long.toString(sessions), Long.toString(coverage), kind};
    }

    /** 键分组聚合入口：把 (站点,小时) → 多行值聚成单行输出（不含键列）。 */
    public static Map<String, String[]> aggregateByKey(List<String[]> parsedRows) {
        Map<String, String[]> grouped = new LinkedHashMap<String, String[]>(); // 保持首次出现顺序，输出前另行排序。
        Map<String, java.util.ArrayList<String[]>> buckets =
                new LinkedHashMap<String, java.util.ArrayList<String[]>>();
        for (String[] row : parsedRows) {
            String key = row[0] + "\t" + row[1];
            java.util.ArrayList<String[]> bucket = buckets.get(key);
            if (bucket == null) {
                bucket = new java.util.ArrayList<String[]>();
                buckets.put(key, bucket);
            }
            String[] valueCols = new String[] {row[2], row[3], row[4], row[5]};
            bucket.add(valueCols);
            grouped.put(key, aggregate(bucket)); // 每次重算，简化为 O(n²) 也足够教学规模。
        }
        return grouped;
    }

    /** 输出行组装：站点、小时 + 聚合列，6 列 TSV。 */
    public static String toRow(String key, String[] aggregated) {
        return key + "\t" + aggregated[0] + "\t" + aggregated[1] + "\t" + aggregated[2] + "\t" + aggregated[3];
    }

    /** BigDecimal → 无指数十进制文本（add 的结果 scale = 操作数最大 scale，与 Python Decimal 一致）。 */
    static String plain(BigDecimal value) {
        return value.toPlainString();
    }

    private static int kindRank(String kind) {
        if (KIND_METER.equals(kind)) {
            return 3;
        }
        if (KIND_SESSION.equals(kind)) {
            return 2;
        }
        return 1;
    }
}
