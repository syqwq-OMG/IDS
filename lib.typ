#import "@preview/zebraw:0.5.5": *
#import "@preview/showybox:2.0.4": showybox
#import emoji: atom

#let doc(name: "孙育泉", id: 10234900421, hw-id: 1, body) = {
  let (en-font, cn-font) = ("New Computer Modern", "Source Han Serif SC")

  set text(font: (en-font, cn-font), weight: 100)
  show raw: set text(font: (
    (name: "Consolas", covers: "latin-in-cjk"),
    "Microsoft YaHei",
  ))

  set heading(numbering: "I.1")
  set par(first-line-indent: 2em)
  set page(numbering: "1", header: context {
    if here().page() == 1 { return none }
    align(center, box(image("src/ecnu-logo.svg"), height: 45%))
  })

  show: zebraw.with(background-color: luma(249),indentation: 4, hanging-indent: true)
  show "。": ". "
  show math.equation: it => {
    show regex("\p{script=Han}"): set text(font: cn-font)
    it
  }
  show heading.where(level: 1): it => text(size: 16pt, it)
  show heading.where(level: 2): it => text(size: 14pt, it)


  align(center, underline(stroke: red + 0.7pt, offset: 5pt, (
    underline(stroke: red + 0.7pt, offset: 3pt, text(size: 20pt, weight: 500, [
      数据科学导论 -HW #hw-id 报告
    ]))
  )))

  align(center, text(size: 13pt)[
    #name #id \
    #datetime.today().display("[year].[month].[day]")
  ])

  body
}


#let remark(body)={
  let col = rgb("#3798e7")
  showybox(
    title: text(font: "New Computer Modern Sans", weight: "bold")[🛈 Remark],
    frame: (
      title-color:col.transparentize(13%),
      body-color:col.transparentize(93%),
      border-color:col,
    ),
    body 
  )
}
