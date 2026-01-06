# Chenalysis

国际象棋数据分析

---

> 报告在 `document/chenalysis` 目录下，演讲幻灯片在 `document/slides` 目录下。

目录结构：

```
.
├── chess-ai.ipynb
├── classify_data.ipynb
├── codes   # 代码
│   ├── build.bat
│   ├── chess.hpp
│   ├── chess_death_map.cpp
│   ├── chess_death_place_visual.ipynb
│   ├── chess_killer_visualizer.ipynb
│   └── ...
├── dataset # 数据集
│   ├── N-enpass.pgn
│   ├── catcatX.pgn
│   ├── compose.pgn
│   ├── experiment.pgn
│   ├── king-take.pgn
│   ├── oyoyaya.pgn
│   └── ...
├── debug   # 调试脚本
│   ├── build.bat
│   ├── build.sh
│   ├── chesskiller.py
│   └── chesskiller_d.cpp
├── deprecated  # 废弃代码
│   ├── chess_killer.ipynb
│   └── chesskiller_multi.cpp
├── document    # 报告和ppt
│   ├── chenalysis  # 报告
│   │   ├── chenalysis.pdf
│   │   ├── chenalysis.typ
│   │   ├── lib.typ
│   │   ├── pic/
│   │   └── ...
│   ├── slides  # ppt
│   │   ├── chenalysis-slide.typ
│   │   ├── chenalysis.pptx
│   │   ├── lib.typ
│   │   └── ...
├── output  # 输出的json数据文件和模型文件
│   ├── chess_death_map/
│   ├── chess_killer/
│   ├── chess_resign/
│   ├── chess_transformer/
│   └── chess_transformer4/
├── prompt  # 提示词和相关文件
│   ├── chess_killer_outcome.txt
│   ├── chess_resign.md
│   ├── chess_resign_visual.md
│   ├── code_file.txt
│   ├── dataset_description.txt
│   ├── death_place.md
├── references
│   ├── 2206.14312v2.pdf
│   ├── 2505.03251v1.pdf
│   └── file170209.PDF
├── readme.md
```

