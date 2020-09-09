COMPONENT_NAME=logring

# we don't compile logring.c since it is static and thus,
# it is already included in the test_logring.cpp file
# SRC_FILES = \
#   $(PROJECT_SRC_DIR)/static/logring.c \

TEST_SRC_FILES = \
  $(UNITTEST_SRC_DIR)/test_logring.cpp

include $(CPPUTEST_MAKFILE_INFRA)
