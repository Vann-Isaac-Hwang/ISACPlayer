### Main

#### Library
Show list of music from selected directories.

#### Pipeline
Graphically configure the pipeline of the route to process the audio stream.
Support importing and exporting config file.
Manage Multiple config files in the config directory.

##### Modules to implement

###### 1. Basic 2I1O Adder (基础 2 输入 1 输出加法器)
* **功能描述**：一个基础的混音算子，接收两路独立的单声道音频输入，按采样点将它们的波形相加并从单一端口输出。内部通常带有防爆音限幅保护，用于将多路音轨或信号进行简单叠加。

###### 2. Basic 2I1O Multipler (基础 2 输入 1 输出乘法器)
* **功能描述**：用于音频幅度缩放的算子。它可以接受控制参数或通过乘以一个固定系数（如 $0.0 \sim 2.0$）来实现数字音量增益调节、静音控制或两路信号的交叉调制。

###### 3. ReplayGain Node (回放增益节点)
* **功能描述**：响度平衡与真峰值限幅节点。它根据音频元数据中的回放增益标签，自动动态调整不同歌曲间的播放电平，确保随机播放时不同录音水平的曲目响度保持一致，并内置前瞻软削波器（True Peak Limiter）防止数字削波失真。

###### 4. Biquad Equalizer Node (双二阶参量均衡器节点)
* **功能描述**：多段参量均衡器（如 10 段 EQ）。利用双二阶滤波器算法（Biquad Filter），对高架、低架、峰值或陷波等特定频段进行增益放大或衰减，用于精细调节音乐的频响曲线和个性化听感。

###### 5. Stereo Widener Node (立体声拓宽节点)
* **功能描述**：空间声场拓宽节点。基于中侧声道（M/S Matrix）处理原理，将立体声分离为中间信号（Mid）和侧边信号（Side），通过单独放大侧边成分来增强音乐的立体感和空间包围感。

###### 6. Convolution Reverb Node (卷积混响节点)
* **功能描述**：空间混响特效节点。通过加载真实的脉冲响应（IR）音频文件，利用频域快速卷积算法，在实时音频流中完美模拟音乐厅、录音棚或特定建筑的真实物理声学反射效果。

###### 7. Visualizer Tap Node (频谱侦听节点)
* **功能描述**：纯侦听与分流节点。它不参与或修改实际的音频信号流，而是利用无锁环形队列（SPSC RingBuffer）异步复制 PCM 数据副本，专门为 UI 界面驱动实时 FFT 频谱分析仪、峰值电平表（VU Meter）提供数据支持。

###### 8. ABX Comparator Node (双盲测试比较器)
* **功能描述**：发烧友音质双盲测试节点。允许用户在两条不同的处理链（如“直通” vs “开启特定算法”）之间进行随机盲测切换，并自动记录多轮盲测结果进行统计学显著性分析，验证主观听感差异。

###### 9. Downsample & Interpolation Node (下采样与插帧节点)
* **功能描述**：采样率与分辨率转换节点。包含下采样器（自带抗混叠低通滤波）和插帧/上采样器（使用线性或样点插值），用于在多声卡或不同时钟体系互联时灵活匹配音频采样率。

###### 10. Crossover Filter Node (电子分频器节点)
* **功能描述**：高阶电子分频节点。使用 Linkwitz-Riley 滤波器将输入的宽频音频信号精准拆分为低频、中频、高频等独立频段输出，专门用于发烧友的多路分频喇叭系统或独立低音炮（Subwoofer）驱动。
