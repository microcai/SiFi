== Si-Fi ==

Si-Fi is the name of my TOY wireless networking technology that uses sound waves to provide
wireless high-speed Internet and network connections.


Si-Fi works on  18Khz-20Khz sound wave range, so Adouts will mostly won't hear it.
But at "Demo" mode, it works on 500 - 1000hz range so people can clearly hear the sounds.

Mulitple devices can co-exist nicely as we are using Frequency-Hopping Spread Spectrum technology.

== 工作原理 ==

=== 采样率和传输率 ===

使用 48khz 采样率， 每个 bit 持续  48 个采样点时间，也就是最大传输速率 1000bps。注意这个是使用了 4B/5B 编码转换后的传输率。
所以真正的传输率为 800bps。

在调制上，信号频率则是逢1跳变。使用 FSK 调制。基频率 N 则输出为 N-p, N+p 的2个频率。p 的取值取决于 N 的频率。为 N 的 八分之一。




