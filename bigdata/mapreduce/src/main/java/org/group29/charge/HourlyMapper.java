package org.group29.charge;

import java.io.IOException;

import org.apache.hadoop.io.LongWritable;
import org.apache.hadoop.io.Text;
import org.apache.hadoop.mapreduce.Mapper;

/**
 * HourlyMapper（T04）：staging TSV 一行 → （站点\t小时）键 + 后四列值。
 * 无效行计数 INVALID_ROWS 并跳过：本应干净的 staging 出现无效行即视为上游故障，
 * 由驱动在作业结束后判定失败，不静默丢弃数据。
 */
public class HourlyMapper extends Mapper<LongWritable, Text, Text, Text> {

    public enum Rows {
        /** 无效行计数：>0 时驱动判失败。 */
        INVALID_ROWS,
        /** 有效行计数：对账用。 */
        VALID_ROWS
    }

    @Override
    protected void map(LongWritable key, Text value, Context context)
            throws IOException, InterruptedException {
        String line = value.toString();
        if (line.isEmpty()) { // 空行忽略：文件尾常见，不算无效。
            return;
        }
        String[] cols;
        try {
            cols = HourlyAggregator.parse(line);
        } catch (IllegalArgumentException error) {
            context.getCounter(Rows.INVALID_ROWS).increment(1); // 拒绝记账：主原因进计数器名。
            context.getCounter("INVALID_ROW_REASONS", error.getMessage()).increment(1);
            return; // 不写输出。
        }
        context.getCounter(Rows.VALID_ROWS).increment(1);
        // 键 = 站点 + TAB + 小时（Text 字典序：站点主序、小时同宽即时间序，输出天然有序）。
        context.write(new Text(cols[0] + "\t" + cols[1]), new Text(cols[2] + "\t" + cols[3] + "\t" + cols[4] + "\t" + cols[5]));
    }
}
