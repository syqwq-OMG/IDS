#import "lib.typ": *


#show: ecnu-theme.with(
  config-info(
    author: [syqwq],
    date: datetime.today().display(),
    institution: [ECNU Department of Data Science and Big Data Technology],
    title: [CHEnalySiS],
    subtitle: [国际象棋数据分析],
  ),
  config-common(
    slide-level: 2,
    // new-section-slide-fn: none,
  ),
)
#show: project-style



#title-slide()

#outline-slide()


#include "sections/intro.typ"
#include "sections/reference.typ"

#focus-slide()[
  Thanks for Listening!
]