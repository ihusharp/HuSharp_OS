

首先是查看 sdb1 的数据区地址

![sb1_start_lba](sb1_start_lba.png)

其中 data_start_lba 位于 0xA6B， root_dir_lba 与 数据起始位置相同

由于 0xA6B = 下图所示 2668

下图上半部分是查看文件写入的数据。由于 file write at lba 0xA6C

因此可得在 1366016 处是 file1 的文件内容

![file1所在地](file1所在地.png)

当写入两个 helloWorld 后。文件内容如下图上半部分。

![inode](inode.png)

由 分区表可知， inode_table 地址位于 0x80B，因此分析如上图。

![inode所在位置](inode所在位置.png)

