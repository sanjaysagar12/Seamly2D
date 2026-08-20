message("Entering test.pro")
TEMPLATE = subdirs
SUBDIRS = \
    ParserTest \
    Seamly2DTest \
    TranslationsTest \
    CollectionTest \
    ActionLayerTest \
    ActionLayerBatchTests

# ActionLayerBatchTests (tests/actionlayer/run_batch) lives outside src/ -- it is the integration
# harness that drives the built seamly2d-actiond binary as a subprocess against every
# tests/actionlayer/scripts/*.json case, rather than a unit test linked against actionlayer
# directly (that's ActionLayerTest, above). 'make check' runs it with no arguments; see its own
# main.cpp and tests/actionlayer/README.md. Requires actiond already built (build_actiond.bat at
# the repo root) -- not expressed as a qmake SUBDIRS dependency since ActionLayerBatchTests does
# not link against actiond at all, only shells out to it at run time.
ActionLayerBatchTests.subdir = $$PWD/../../tests/actionlayer/run_batch
