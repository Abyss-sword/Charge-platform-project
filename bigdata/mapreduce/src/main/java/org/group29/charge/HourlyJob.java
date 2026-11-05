package org.group29.charge;

import org.apache.hadoop.conf.Configured;
import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.io.Text;
import org.apache.hadoop.mapreduce.Job;
import org.apache.hadoop.mapreduce.lib.input.TextInputFormat;
import org.apache.hadoop.mapreduce.lib.output.TextOutputFormat;
import org.apache.hadoop.util.Tool;
import org.apache.hadoop.util.ToolRunner;

/**
 * HourlyJob（T04 驱动）：充电平台小时聚合业务作业（不是 WordCount 替代品）。
 *
 * 用法：hadoop jar charge-hourly.jar org.group29.charge.HourlyJob --input <HDFS路径> --output <HDFS路径>
 * 退出码约定：0 成功；2 参数错误（缺失/未知参数）；3 输入不存在；4 输出已存在；
 * 1 作业失败或无效行计数 > 0（本应干净的 staging 出现无效行即判上游故障）。
 * 本地模式：hadoop jar ... -fs local -jt local --input <本地目录> --output <本地目录>
 */
public class HourlyJob extends Configured implements Tool {

    private static final String INPUT_FLAG = "--input";
    private static final String OUTPUT_FLAG = "--output";

    public static void main(String[] args) throws Exception {
        // Hadoop 3.2.1 RunJar 在 manifest 含 Main-Class 时不剥离显式类名；且 GenericOptionsParser
        // 遇到首个非通用参数即停止，因此必须在 ToolRunner 之前剥离类名，否则 -fs/-jt 不被解析。
        if (args.length > 0 && args[0].endsWith("HourlyJob")) {
            String[] rest = new String[args.length - 1];
            System.arraycopy(args, 1, rest, 0, rest.length);
            args = rest;
        }
        int exitCode = ToolRunner.run(new HourlyJob(), args); // 支持 -fs/-jt 等 GenericOptions。
        System.exit(exitCode);
    }

    @Override
    public int run(String[] args) throws Exception {
        String input = null; // 必填：staging TSV 目录。
        String output = null; // 必填：hourly 输出目录（不得覆盖旧输出）。
        for (int i = 0; i < args.length; i++) {
            if (INPUT_FLAG.equals(args[i]) && i + 1 < args.length) {
                input = args[++i];
            } else if (OUTPUT_FLAG.equals(args[i]) && i + 1 < args.length) {
                output = args[++i];
            } else {
                System.err.println("用法: HourlyJob --input <路径> --output <路径>（未知参数: " + args[i] + "）");
                return 2; // 参数错误。
            }
        }
        if (input == null || output == null) { // 两个必填参数缺一退出 2。
            System.err.println("用法: HourlyJob --input <路径> --output <路径>");
            return 2;
        }
        org.apache.hadoop.conf.Configuration conf = getConf();
        FileSystem fs = FileSystem.get(conf);
        if (!fs.exists(new Path(input))) { // 输入不存在。
            System.err.println("输入不存在: " + input);
            return 3;
        }
        Path outputPath = new Path(output);
        if (fs.exists(outputPath)) { // 输出已存在：不得覆盖旧输出。
            System.err.println("输出已存在: " + output);
            return 4;
        }
        Job job = Job.getInstance(conf, "charge-hourly-aggregation");
        job.setJarByClass(HourlyJob.class);
        job.setMapperClass(HourlyMapper.class);
        job.setReducerClass(HourlyReducer.class);
        job.setMapOutputKeyClass(Text.class);
        job.setMapOutputValueClass(Text.class);
        job.setOutputKeyClass(Text.class);
        job.setOutputValueClass(Text.class);
        job.setInputFormatClass(TextInputFormat.class);
        job.setOutputFormatClass(TextOutputFormat.class);
        TextInputFormat.addInputPath(job, new Path(input));
        TextOutputFormat.setOutputPath(job, outputPath);
        boolean succeeded = job.waitForCompletion(true); // 等待作业完成且成功才可能退出 0。
        long invalidRows = job.getCounters().findCounter(HourlyMapper.Rows.INVALID_ROWS).getValue();
        long validRows = job.getCounters().findCounter(HourlyMapper.Rows.VALID_ROWS).getValue();
        System.out.println("JOB_ID=" + job.getJobID());
        System.out.println("JOB_STATUS=" + (succeeded ? "SUCCEEDED" : "FAILED"));
        System.out.println("VALID_ROWS=" + validRows);
        System.out.println("INVALID_ROWS=" + invalidRows);
        if (!succeeded) {
            return 1;
        }
        if (invalidRows > 0) { // 无效行 > 0：staging 本应干净，判失败并明确报告。
            System.err.println("发现无效行 " + invalidRows + " 条，作业判失败（不静默丢弃）");
            return 1;
        }
        System.out.println("OUTPUT_PATH=" + outputPath);
        return 0;
    }
}
