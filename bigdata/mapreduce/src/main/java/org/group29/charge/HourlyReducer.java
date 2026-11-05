package org.group29.charge;

import java.io.IOException;
import java.util.Iterator;

import org.apache.hadoop.io.Text;
import org.apache.hadoop.mapreduce.Reducer;

/**
 * HourlyReducer（T04）：同一（站点,小时）键的电量用 BigDecimal 精确累加，
 * 输出固定 6 列 TSV（键字典序：站点主序、UTC 小时同宽即时间序）。
 * 订单数（会话开始计数）只在会话起始小时累加一次，由上游分摊保证，此处只做求和。
 */
public class HourlyReducer extends Reducer<Text, Text, Text, Text> {

    @Override
    protected void reduce(Text key, Iterable<Text> values, Context context)
            throws IOException, InterruptedException {
        // 把 Text 值转成字符串列数组交给纯函数聚合（BigDecimal 累加、覆盖取最大、类型取优）。
        java.util.List<String[]> columns = new java.util.ArrayList<String[]>();
        Iterator<Text> iterator = values.iterator();
        while (iterator.hasNext()) {
            String[] parts = iterator.next().toString().split("\t", -1);
            columns.add(parts);
        }
        String[] aggregated = HourlyAggregator.aggregate(columns);
        context.write(key, new Text(aggregated[0] + "\t" + aggregated[1] + "\t" + aggregated[2] + "\t" + aggregated[3]));
    }
}
