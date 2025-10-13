= schema
当然，我们来详细讨论一下什么是合适的数据库 Schema (结构)，以及如何针对你的项目需求进行优化。

根据你的项目要求和数据特点，一个“优化”的结构应该是：

1.  **逻辑清晰**：能够直观地反映你的数据。
2.  [cite\_start]**查询高效**：让你能快速地执行分析查询 [cite: 13, 14, 15]。
3.  **避免冗余**：在合理的范围内减少重复数据。
4.  **保证数据完整性**：确保存入的数据是有效和一致的。

### [cite\_start]合适的 Schema 设计 [cite: 12]

在之前的对话中，我们已经设计了一个非常合适的 Schema。现在我们来深入分析一下为什么它是优化的。

**表名**: `esi_rankings`

| 列名 (Column Name) | 数据类型 (Data Type) | 约束/说明 (Constraints/Notes) |
| :--- | :--- | :--- |
| `id` | `INTEGER` | **主键 (PRIMARY KEY)**, `AUTOINCREMENT`. 每一行的唯一标识，数据库自动生成。 |
| `research_field` | `TEXT` | **不能为空 (NOT NULL)**. 关键的分类字段，如 "CHEMISTRY", "ENGINEERING"。 |
| `rank` | `INTEGER` | 机构在该学科下的排名。 |
| `institution` | `TEXT` | **不能为空 (NOT NULL)**. 机构名称。 |
| `country_region` | `TEXT` | 国家或地区。 |
| `documents` | `INTEGER` | Web of Science 文献数。 |
| `cites` | `INTEGER` | 总引用数。 |
| `cites_per_paper` | `REAL` | 篇均引用数。使用 `REAL` (浮点数) 来精确存储小数。 |
| `top_papers` | `INTEGER` | 顶尖论文数。 |

**SQL 创建语句:**

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

### 为什么这个 Schema 是经过优化的？

#### 1\. 单一表的简洁性 (Denormalization)

你可能会想，是否需要为“机构”或“国家”创建单独的表？比如一个 `institutions` 表和一个 `countries` 表。

对于你这个项目来说，**不需要**。原因如下：

  * [cite\_start]**查询简单**：你的所有分析查询（如查询华东师范大学 [cite: 13][cite\_start]、查询中国大学 [cite: 14]）都可以在这一个表上通过简单的 `WHERE` 和 `GROUP BY` 完成。如果分表，你就需要使用 `JOIN` 操作，这会让查询变得更复杂。
  * **数据分析场景**：你当前的需求是数据分析和报告，而不是一个需要频繁增删改查的在线交易系统 (OLTP)。在这种“读多写少”的场景下，适当的数据冗余（比如重复存储机构名和国家名）来换取查询速度和简便性，是一种常见的优化策略，这被称为“反规范化”或“反模式化 (Denormalization)”。

#### 2\. 数据类型的正确选择

  * 我们为排名、文献数等整数选择了 `INTEGER`。
  * 为篇均引用数选择了 `REAL`，这可以存储小数，保证了数据的精度。
  * 为名称和类别选择了 `TEXT`。
    正确的类型选择不仅能节省存储空间，还能确保数据的正确性（例如，你无法在 `INTEGER` 列中存入文本）。

#### 3\. 约束保证数据质量

  * `PRIMARY KEY`: 保证了每一行数据的唯一性。
  * `NOT NULL`: 我们在 `institution` 和 `research_field` 上加了 `NOT NULL` 约束。这非常重要，它从数据库层面阻止了无效数据（比如我们之前遇到的空行）的插入，保证了核心数据的完整性。

### 如何进一步优化：创建索引 (Indexing)

这是最重要的一步优化，它能**极大提升查询速度**。索引就像一本书的目录，数据库可以通过索引快速定位到它需要的数据，而不需要逐行扫描整个表。

根据你的作业要求，你会频繁地根据以下三个字段进行查询：

1.  [cite\_start]`institution` (查询华东师范大学 [cite: 13])
2.  [cite\_start]`country_region` (查询中国大学的表现 [cite: 14][cite\_start]，分析全球区域表现 [cite: 15])
3.  [cite\_start]`research_field` (在各个学科中查询 [cite: 13, 14, 15])

因此，我们应该在这三个列上分别创建索引。

**怎么做？**

在你已经将所有数据导入数据库之后，连接到你的 SQLite 数据库并执行以下 SQL 语句：

```sql
-- 为 institution 列创建索引
CREATE INDEX idx_institution ON esi_rankings (institution);

-- 为 country_region 列创建索引
CREATE INDEX idx_country_region ON esi_rankings (country_region);

-- 为 research_field 列创建索引
CREATE INDEX idx_research_field ON esi_rankings (research_field);
```

执行完这三条命令后，你的数据库就已经针对你的特定分析需求做好了性能优化。当你执行 `WHERE institution = 'EAST CHINA NORMAL UNIV'` 这样的查询时，速度会比没有索引时快得多。

### 总结

你的最佳实践步骤如下：

1.  [cite\_start]**使用我们设计的单表 Schema** [cite: 12]。这是最适合你当前项目需求的、逻辑清晰且简单的结构。
2.  **在数据导入完成后，为 `institution`、`country_region` 和 `research_field` 这三列创建索引**。这是提升你后续 SQL 分析查询性能的关键步骤。

这样，你的数据库结构就既合理又经过了优化，完全可以满足你后续的作业要求。