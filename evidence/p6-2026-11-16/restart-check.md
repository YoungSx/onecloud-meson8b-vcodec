# 优雅重启验收（2026-11-17）

补上 [冷启动验收](reboot-autoload.md) 明确留下的空缺：那次是复位后的冷启动，
`systemctl restart` 这条路径没单独跑过。

## 为什么是另一条路径

单元是 `Type=oneshot` + `RemainAfterExit=yes`，`ExecStop` 调
`runtime-device.py unload`（撤 overlay、卸三个模块），`ExecStart` 调 `load`。
`systemctl restart` 在一次事务里连续做 stop 和 start，中间没有重启来清硬件状态，
这是最容易留下残留的路径。

## 方法

脚本 [scripts/restart-check.sh](../../scripts/restart-check.sh)：

1. 若 `/dev/video*` 被占用则拒绝执行（卸载会把模块从在用客户端底下抽走）。
2. 记录 dmesg 行号，`systemctl restart`，计时。
3. 等最多 5 s，检查两节点是否恢复。
4. 截取本次重启的 dmesg 片段。
5. 解码冒烟：同一条 MPEG-4 流，确认重启后仍能出帧。

## 结果

```
state before: active enabled
nodes before: /dev/video0 /dev/video1
restart rc=0 elapsed=5s
state after: active
show: Result=success ExecMainStatus=0
nodes after: /dev/video0 /dev/video1
decode smoke frames=22
```

本次重启的 dmesg：

```
[23677.495318] meson8b-runtime: applied volatile codec overlay 1
[23677.534553] meson-vdec d0050000.video-decoder: registered meson-video-decoder as /dev/video0
[23677.534579] meson-vdec d0050000.video-decoder: Meson8b capture uses separate references and CPU byte-order conversion
[23677.564874] meson-venc d0050000.video-encoder: registered meson-video-encoder as /dev/video1
```

卸载再加载没有留下重复注册、告警或错误。解码冒烟 22 帧与重启前一致。

原始日志：[restart/restart.log](restart/restart.log)。

## 未覆盖

- 只做了解码冒烟，没有在重启后跑全部 codec 回归、编码和并发。
- 只重启了一次，没有做多次连续重启（模块反复加载/卸载）的耐久测试。
- 重启时无客户端在用；有客户端时 `systemctl restart` 的行为（客户端收到什么）
  未测，脚本本身会拒绝这种情形。
