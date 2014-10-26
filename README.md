== Si-Fi ==

Si-Fi is the name of my TOY wireless networking technology that uses sound waves to provide
wireless high-speed Internet and network connections.


Si-Fi works on  18Khz-20Khz sound wave range, so Adouts will mostly won't hear it.
But at "Demo" mode, it works on 1200 - 2200hz range so people can clearly hear the sounds.

Mulitple devices can co-exist nicely as we are using Frequency-Hopping Spread Spectrum technology.

== 工作原理 ==

=== 采样率和传输率 ===

使用 48khz 采样率， 每个 bit 持续  48 个采样点时间，也就是最大传输速率 1000bps。注意这个是使用了 4B/5B 编码转换后的传输率。
所以真正的传输率为 800bps。

在调制上，信号频率则是逢1跳变。使用 2FSK 调制。基频率 N 则输出为 N-p, N+p 的2个频率。p 的取值取决于 N 的频率。为 N 的 八分之一。

=== 信道编码 ===

信道编码使用 4b5b 编码然后逢1跳变。因为 4b5b 编码不会出现 0b11111 ，所以用  0b11111 作为帧同步序列。
同时使用 0b00000 作为解调器同步序列。收到 长 0 序列后，解码器重启，然后监听 11111 序列。出现后即锁定一帧。

=== 帧结构 ===
帧使用不定长（但是限制 1s 以内发完），如果同时发射多帧，可以使用 11111 隔开。
帧包含一个固定的帧头和校验位

