package org.group29.charge;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;

import java.math.BigDecimal;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

import org.junit.Test;

/**
 * HourlyAggregator 纯 Java 单元测试（T04，失败测试→最小实现→复测）。
 * 覆盖：BigDecimal 累计精度、乱序输入一致、同站分组、跨站分组、无效行拒绝、
 * 输出行格式与 Python Decimal 参考实现的字符串一致性。
 */
public class HourlyAggregationTest {

    private static String[] cols(String energy, String sessions, String coverage, String kind) {
        return new String[] {energy, sessions, coverage, kind};
    }

    /** Decimal 累计：5.135 + 5.135 = 10.270（scale 保持 3，与 Python Decimal 加法一致）。 */
    @Test
    public void testBigDecimalAccumulationKeepsScale() {
        String[] result = HourlyAggregator.aggregate(Arrays.asList(
                cols("5.135", "0", "3600", "SESSION_ALLOCATED"),
                cols("5.135", "1", "3600", "SESSION_ALLOCATED")));
        assertEquals("10.270", result[0]);
        assertEquals("1", result[1]); // 会话开始只在起始小时计一次：跨小时第二段不再计。
        assertEquals("3600", result[2]);
        assertEquals("SESSION_ALLOCATED", result[3]);
    }

    /** 乱序输入与有序输入结果完全一致（BigDecimal 加法交换律）。 */
    @Test
    public void testUnorderedInputSameResult() {
        String[] ordered = HourlyAggregator.aggregate(Arrays.asList(
                cols("7.30", "1", "3600", "METER_INTERVAL"),
                cols("3.075", "0", "1800", "METER_INTERVAL")));
        String[] unordered = HourlyAggregator.aggregate(Arrays.asList(
                cols("3.075", "0", "1800", "METER_INTERVAL"),
                cols("7.30", "1", "3600", "METER_INTERVAL")));
        assertEquals(ordered[0], unordered[0]);
        assertEquals("10.375", unordered[0]); // 7.30 + 3.075 = 10.375。
        assertEquals("3600", unordered[2]); // 覆盖取最大而非求和（求和会是 5400）。
    }

    /** 同站同小时多行聚成一行；跨站同小时分开成两行，键按站点字典序。 */
    @Test
    public void testGroupingSameAndCrossStation() {
        List<String[]> rows = new ArrayList<String[]>();
        rows.add(new String[] {"sim:station-b", "2026-09-01T09:00:00Z", "4.80", "0", "3600", "PUBLIC_HOURLY"});
        rows.add(new String[] {"sim:station-a", "2026-09-01T09:00:00Z", "7.30", "1", "3600", "METER_INTERVAL"});
        rows.add(new String[] {"sim:station-a", "2026-09-01T10:00:00Z", "3.075", "1", "1800", "METER_INTERVAL"});
        rows.add(new String[] {"sim:station-a", "2026-09-01T09:00:00Z", "1.00", "0", "1800", "METER_INTERVAL"});
        Map<String, String[]> grouped = HourlyAggregator.aggregateByKey(rows);
        assertEquals(3, grouped.size()); // (b,09) (a,09) (a,10) 三个键。
        assertEquals("8.30", grouped.get("sim:station-a\t2026-09-01T09:00:00Z")[0]); // 同站同小时累计。
        assertEquals("4.80", grouped.get("sim:station-b\t2026-09-01T09:00:00Z")[0]); // 跨站不互借。
    }

    /** 无效行拒绝：负电量、列数错误、未知类型、覆盖越界、空站点都必须抛异常。 */
    @Test
    public void testInvalidRowsRejected() {
        assertRejected("sim:station-a\t2026-09-01T09:00:00Z\t-5.00\t1\t3600\tMETER_INTERVAL");
        assertRejected("sim:station-a\t2026-09-01T09:00:00Z\t5.00\t1\t3600"); // 5 列。
        assertRejected("sim:station-a\t2026-09-01T09:00:00Z\t5.00\t1\t3600\tWORDCOUNT"); // 未知类型。
        assertRejected("sim:station-a\t2026-09-01T09:00:00Z\t5.00\t1\t7200\tMETER_INTERVAL"); // 覆盖>3600。
        assertRejected("\t2026-09-01T09:00:00Z\t5.00\t1\t3600\tMETER_INTERVAL"); // 空站点。
        assertRejected("sim:station-a\t2026-09-01T09:00:00Z\t1.0E2\t1\t3600\tMETER_INTERVAL"); // 科学计数法。
    }

    private void assertRejected(String line) {
        try {
            HourlyAggregator.parse(line);
            fail("应当拒绝: " + line);
        } catch (IllegalArgumentException expected) {
            // 预期路径：拒绝码见 HourlyAggregator 常量。
        }
    }

    /** 输出行组装：键含制表符，最终 6 列与 MR TextOutputFormat 的 key\tvalue 拼接一致。 */
    @Test
    public void testOutputRowFormat() {
        String key = "sim:station-a\t2026-09-03T09:00:00Z";
        String[] aggregated = HourlyAggregator.aggregate(java.util.Collections.singletonList(
                cols("5.135", "1", "3600", "SESSION_ALLOCATED")));
        String row = HourlyAggregator.toRow(key, aggregated);
        assertEquals(6, row.split("\t", -1).length); // 固定 6 列。
        assertEquals("sim:station-a\t2026-09-03T09:00:00Z\t5.135\t1\t3600\tSESSION_ALLOCATED", row);
    }

    /** 精度边界：0 + 0.00 = 0.00；两位与三位小数相加 scale 取最大。 */
    @Test
    public void testZeroAndScale() {
        assertEquals("0.00", HourlyAggregator.aggregate(Arrays.asList(
                cols("0", "0", "0", "PUBLIC_HOURLY"), cols("0.00", "0", "0", "PUBLIC_HOURLY")))[0]);
        assertEquals(new BigDecimal("10.270"), new BigDecimal("5.135").add(new BigDecimal("5.135")));
    }
}
