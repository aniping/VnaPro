# 校准池文件引用

二进制校准池以 `channel-N.json` 的 version 2 manifest 列出 `.vcal` 文件、校准集 ID 和完整文件校验值。除服务生成的不可变事务文件外，也支持预先生成的 `校准集名称.vcal`，例如 `S5P1_S11P1-S19P4_S13P1.vcal`。读取后必须核对二进制中的名称、ID 和校验值；仅改文件名而不更新内容和 manifest 会拒绝加载。

命名文件必须是单个 basename，名称部分最多 128 个 UTF-8 字节，不允许路径分隔符、Windows 非法字符、设备保留名或末尾空格/句点。`channel-` 前缀保留给服务内部事务文件。目录中存在合法的命名 `.vcal` 而缺少对应通道的 manifest 时，加载和直接保存均拒绝操作，避免把已有池误当空池。

启动时完整恢复命名文件；随后仅切换选择会复用名称、二进制文件和校验值，不重新读取或改写系数。修改校准内容时仍写新的内部不可变文件，再原子发布 manifest。服务的垃圾回收只清理自己的事务文件，不自动删除外部命名文件；删除这类校准集后，原命名文件由用户管理。

验证：`ctest -R '^(calibration_store|calibration_named_store)$' --output-on-failure`。

# 软件扫频时间

`SENS<n>:SWE:TIME <seconds>` 设置通道手动时间并关闭 Auto，支持 S、MS、US、NS 单位；时间必须有限且在 `(0,100000]` 秒。`SENS<n>:SWE:TIME:AUTO ON|OFF|1|0` 切换 Auto，`AUTO?` 查询开关；OFF 保留上次手动时间。`TIME?` 在手动状态返回请求值，Auto 状态返回当前配置最近完整帧的实测主机耗时，首次尚无结果时为 0。`TIME? MAX[IMUM]` 返回 100000；没有硬件最短时间估算，`TIME? MIN[IMUM]` 明确报 -221，可使用 `TIME:AUTO ON`。

VN10 的手动时间通过可中断的主机回读节拍实现，是每轮请求的最小回读时间；不改变 ADC 积分、RF 驻留或 IFBW，实际完整扫描可能更长。Native08 不支持该节拍，通过能力字段报告并拒绝手动设置。Auto 沿用原采集速度。

`POST /api/v1/config` 在原有 requestId、expectedRevision 与控制会话规则下接受 `sweepTimeAuto`（严格布尔）及 `sweepTimeSeconds`（秒）。单独设置时间关闭 Auto；同时显式提供 Auto 时，以显式值为准。`GET /api/v1/status` 的顶层与各通道 config 返回这两个字段，顶层及通道 `lastSweepTimeSeconds` 返回实测值或未知值 0，capabilities 返回 `manualSweepTimeSupported` 与 `maxSweepTimeSeconds`。配置变化清空旧实测值；运行时扫时不写入校准文件。

长 Single 执行中，当前控制者可通过 `/api/v1/sweep-control` 的 `stop`、旧 `/api/v1/operations` 的 `hold` 或同 SCPI 连接的 `SENS:SWE:MODE HOLD` 唤醒软件等待，安全读完剩余 IQ，再完成 FIFO 中的正式事务。`INIT:CONT OFF` 同样停止并保留 Single 选择。已提交的 `*OPC?` 或 `*WAI` 是显式同步屏障，其后同连接 HOLD 仍需等待该屏障完成；需要停止在途 Single 时，直接发送 HOLD，不先加入等待完成命令。

验证：`ctest -R '^(service_sweep_time|sweep_time_protocol|sweep_time_public_stop)$' --output-on-failure`。
