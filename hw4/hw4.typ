#import "../lib.typ": *
#import "@preview/callisto:0.2.4"
#import "@preview/cmarker:0.1.6"
#show: doc.with(hw-id: 4)

#let (render, result) = callisto.config(nb: json("hw4.ipynb"))

= 实验要求
对于 ESI 数据的获取方面，可以查看 `hw3` 目录下的文件，不再赘述。这次我们已经获取到了 `csv` 文件，要求如下：
+ 将获取的数据导入到一个关系型数据库系统中（系统可以自选）

+ 优化关系型数据，并整理一个合理的schema
+ 通过写SQL语句，获取华东师范大学在各个学科中的排名
+ 通过写SQL语句，获取中国（大陆地区）大学在各个学科中的表现
+ 通过写SQL语句，分析全球不同区域在各个学科中的表现

= 具体实现
== 建立数据库与导入数据

由于本次实验的任务较轻，且便于github的展示，这里选用轻量级的 `SQLite` 作为数据库系统。

首先，引入所需的库文件，检查数据库是否已经存在，如果存在则删除，防止重复插入。我们先要创建数据库的表结构：

```sql 
CREATE TABLE IF NOT EXISTS esi_rankings (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    research_field TEXT NOT NULL,
    rank INTEGER,
    institution TEXT NOT NULL,
    country_region TEXT,
    documents INTEGER,
    cites INTEGER,
    cites_per_paper REAL,
    top_papers INTEGER
);
```
接着，我们将 `csv` 文件中的数据条目插入到数据库中。在这里使用 `pandas` 读取 `csv` 文件，然后使用 `to_sql` 方法将数据插入到数据库中。

这里要注意的是，所给文件的编码是 `latin-1`，并非 `utf-8`，所以在读取时要指定编码格式。以及有些数据可能会有空缺，要进行处理。


// 注意编码，还有空行
#render(0)

至此，创建数据库工作完成。

== 选择合适的 Schema

根据这个项目要求和数据特点，一个“好”的结构应该是：

1.  *逻辑清晰*：能够直观地反映数据。

2.  *查询高效*：能快速地执行分析查询。
3.  *避免冗余*：在合理的范围内减少重复数据。
4.  *保证数据完整性*：确保存入的数据是有效和一致的。

先来看一下现在的schema：

#cmarker.render(
"
| 列名 (Column Name) | 数据类型 (Data Type) | 约束/说明 (Constraints/Notes) |
| :--- | :--- | :--- |
| `id` | `INTEGER` | *主键 (PRIMARY KEY)*, `AUTOINCREMENT`. 每一行的唯一标识，数据库自动生成。 |
| `research_field` | `TEXT` | *不能为空 (NOT NULL)*. 关键的分类字段，如 \"CHEMISTRY\", \"ENGINEERING\"。 |
| `rank` | `INTEGER` | 机构在该学科下的排名。 |
| `institution` | `TEXT` | *不能为空 (NOT NULL)*. 机构名称。 |
| `country_region` | `TEXT` | 国家或地区。 |
| `documents` | `INTEGER` | Web of Science 文献数。 |
| `cites` | `INTEGER` | 总引用数。 |
| `cites_per_paper` | `REAL` | 篇均引用数。使用 `REAL` (浮点数) 来精确存储小数。 |
| `top_papers` | `INTEGER` | 顶尖论文数。 |
"
)
事实上，这样的安排已经很好了。虽然，我们后续要查询“机构”或者“国家”，但是这些字段并不适合单独拆分成表格，因为它们的种类太多，且没有明显的层级关系。拆分成表格反而会增加查询的复杂度，在这张表中，我们通过简单的 `WHERE` 和 `GROUP BY` 就可以完成。如果分表，就需要使用 `JOIN` 操作，这会让查询变得更复杂。

== 获取 ECNU 在各个学科中的排名
为了方便后续的代码编写，我们先定义如下几个东西：
- `execute_query()`：执行SQL查询的函数

- `get_iso_alpha_3()`：获取国家的ISO Alpha-3代码的函数，用于最后一步实现全球可视化
- `DATABASE_TITLE`：数据库的路径
#render(1)

这里的处理是，为了方便后续的对比，我们定义了一个所查机构的列表，用于遍历。 只需要在查询到 ECNU 的时候进行输出即可。这里的查询语句就是看机构的名字是否与 `institution` 字段匹配。注意，我们使用了 `LIKE` 语句，而不是 `=`，为了提高我们查询的容错。

#render(2)

为了方便对比，这里罗列了一些高校在所有学科中的排名。（注意：越低越好 hhh）

#render(3)


== 获取中国（大陆地区）大学在各个学科中的表现
这里，我们使用了 `country_region` 字段来筛选出中国（大陆地区）的大学。然后，我们考虑两种指标:
+ 在每个学科中，有多少所中国大陆的大学进入了全球排名前100名

+ 在每个学科中，排名第一的中国大陆大学

既注重整体，也注重代表。
#render(4)


== 分析全球不同区域在各个学科中的表现
这里，为了以地区为单位，我们在查询的时候，使用了 `country_region` 字段进行分组。我们统计了每个国家在各个学科中的总引用数并进行了可视化。

```python 
print("正在获取全球各学科数据用于生成世界地图...")
subject_data_query = """
    SELECT
      country_region,
      research_field,
      SUM(cites) AS total_cites
    FROM
      esi_rankings
    GROUP BY
      country_region, research_field;
"""
df_map = execute_query(DATABASE_FILE, subject_data_query)

if df_map is not None:
    print("数据预处理中：清洗、转换ISO代码...")
    df_map.dropna(subset=["country_region", "research_field"], inplace=True)
    df_map["total_cites"] = pd.to_numeric(
        df_map["total_cites"], errors="coerce"
    ).fillna(0)
    df_map["iso_alpha"] = df_map["country_region"].apply(
        get_iso_alpha_3
    )  # 调用单元格1的函数

    subjects = sorted(df_map["research_field"].unique())

    print("正在生成交互式地图...")
    fig = go.Figure()

    for subject in subjects:
        subject_df = df_map[df_map["research_field"] == subject]
        log_cites = np.log10(
            subject_df["total_cites"] + 1
        )  # 对引用数取对数，以优化颜色显示

        fig.add_trace(
            go.Choropleth(
                locations=subject_df["iso_alpha"],
                z=log_cites,
                text=subject_df["country_region"],
                customdata=subject_df["total_cites"],
                hovertemplate="<b>%{text}</b><br>总引用数: %{customdata:,.0f}<extra></extra>",
                colorscale="Plasma",
                visible=(subject == subjects[0]),  # 默认只显示第一个学科
            )
        )

    buttons = []
    for i, subject in enumerate(subjects):
        visibility_mask = [False] * len(subjects)
        visibility_mask[i] = True
        buttons.append(
            dict(label=subject, method="update", args=[{"visible": visibility_mask}])
        )

    fig.update_layout(
        title_text=f"<b>全球各学科学术影响力地图</b><br><i>请从下拉菜单中选择一个学科</i>",
        title_x=0.5,
        title_font_size=22,
        margin=dict(t=70, l=0, r=0, b=0),
        updatemenus=[
            dict(
                active=0,
                buttons=buttons,
                direction="down",
                pad={"r": 10, "t": 10},
                showactive=True,
                x=0.1,  # 将菜单的x坐标设置在左侧10%的位置
                xanchor="left",  # x坐标的锚点是菜单的左边缘
                y=1.1,  # 将菜单的y坐标放在新的顶部空间内
                yanchor="top",  # y坐标的锚点是菜单的上边缘
            )
        ],
        geo=dict(
            showframe=False, showcoastlines=False, projection_type="natural earth"
        ),
    )

    fig.show()
else:
    print("未能获取用于生成地图的数据。")
```

这里只截取了 CS 学科的结果截图：

#figure(
  image("plot.png"),
  caption: [全球机构 - computer science]
)<cs>

#remark[
  - 完整代码见 `hw4.ipynb`。

  - 最后一部分为交互图表，在 `pdf` 文件中无法展示，可以打开 `jupyter notebook` 查看。
]