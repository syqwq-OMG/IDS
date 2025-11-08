#import "../lib.typ":*
#import "@preview/callisto:0.2.4"
#show:doc.with(hw-id: 7)

#let (render, result) = callisto.config(nb: json("hw7.ipynb"))

= 实验要求
本次作业主要是改进上次的作业，这次主要改进的任务是：
1. 在上一节课作业的基础上，请利用深度学习方法，对各学科做一个排名模型，能够较好的预测出排名位置，并且利用MSE，MAPE等指标来进行评价模型的优劣。


= 具体实现
本次使用表格深度学习的方法。


== 准备工作

首先，引入所需的库文件，方便之后的数据分析、数据库导入、可视化等工作。
#render(0)

然后，定义数据库的路径、学校的名称等变量，方便后续使用。
#render(1)

接下来，类似上次作业的代码，定义加载数据库、数据聚合的函数，并且调用。完成数据的加载与预处理工作。
#render(2)

#render(3)

== 深度学习模型构建与训练

+ 对目标 $y$ (rank) 进行 `np.log()` 变换，使分布更平滑，便于神经网络学习。

+ 对数值特征使用 `StandardScaler` (因为是对原始数据，不是聚合数据，`StandardScaler` 在这里是合适的)。

+ 对类别特征使用 `OneHotEncoder`。

+ 对数据集进行打乱后，按照 80% / 20% 划分训练集和测试集，然后进行分层抽样，尽可能让训练集与测试集同分布。

#render(4)

接下来，构建一个简单的多层感知机（MLP）.
#render(5)

然后，训练模型。

#render(6)

- 按作业要求，我们使用 `mean_squared_error` 和 `mean_absolute_percentage_error` 进行评估。

- 我们必须先用 `np.exp()` 将模型的预测值（log(rank)）还原回 rank，才能与 `y_test_orig` (原始排名) 进行比较。

#render(7)


#remark[
  完整代码见 `./hw7.ipynb`.
]