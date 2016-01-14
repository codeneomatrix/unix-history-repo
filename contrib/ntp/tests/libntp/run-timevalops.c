/* AUTOGENERATED FILE. DO NOT EDIT. */

//=======Test Runner Used To Run Each Test Below=====
#define RUN_TEST(TestFunc, TestLineNum) \
{ \
  Unity.CurrentTestName = #TestFunc; \
  Unity.CurrentTestLineNumber = TestLineNum; \
  Unity.NumberOfTests++; \
  if (TEST_PROTECT()) \
  { \
      setUp(); \
      TestFunc(); \
  } \
  if (TEST_PROTECT() && !TEST_IS_IGNORED) \
  { \
    tearDown(); \
  } \
  UnityConcludeTest(); \
}

//=======Automagically Detected Files To Include=====
#include "unity.h"
#include <setjmp.h>
#include <stdio.h>
#include "config.h"
#include "ntp_types.h"
#include "ntp_fp.h"
#include "timevalops.h"
#include <math.h>

//=======External Functions This Runner Calls=====
extern void setUp(void);
extern void tearDown(void);
extern void test_Helpers1(void);
extern void test_Normalise(void);
extern void test_SignNoFrac(void);
extern void test_SignWithFrac(void);
extern void test_CmpFracEQ(void);
extern void test_CmpFracGT(void);
extern void test_CmpFracLT(void);
extern void test_AddFullNorm(void);
extern void test_AddFullOflow1(void);
extern void test_AddUsecNorm(void);
extern void test_AddUsecOflow1(void);
extern void test_SubFullNorm(void);
extern void test_SubFullOflow(void);
extern void test_SubUsecNorm(void);
extern void test_SubUsecOflow(void);
extern void test_Neg(void);
extern void test_AbsNoFrac(void);
extern void test_AbsWithFrac(void);
extern void test_Helpers2(void);
extern void test_ToLFPbittest(void);
extern void test_ToLFPrelPos(void);
extern void test_ToLFPrelNeg(void);
extern void test_ToLFPabs(void);
extern void test_FromLFPbittest(void);
extern void test_FromLFPrelPos(void);
extern void test_FromLFPrelNeg(void);
extern void test_LFProundtrip(void);
extern void test_ToString(void);


//=======Test Reset Option=====
void resetTest(void);
void resetTest(void)
{
  tearDown();
  setUp();
}

char const *progname;


//=======MAIN=====
int main(int argc, char *argv[])
{
  progname = argv[0];
  UnityBegin("timevalops.c");
  RUN_TEST(test_Helpers1, 39);
  RUN_TEST(test_Normalise, 40);
  RUN_TEST(test_SignNoFrac, 41);
  RUN_TEST(test_SignWithFrac, 42);
  RUN_TEST(test_CmpFracEQ, 43);
  RUN_TEST(test_CmpFracGT, 44);
  RUN_TEST(test_CmpFracLT, 45);
  RUN_TEST(test_AddFullNorm, 46);
  RUN_TEST(test_AddFullOflow1, 47);
  RUN_TEST(test_AddUsecNorm, 48);
  RUN_TEST(test_AddUsecOflow1, 49);
  RUN_TEST(test_SubFullNorm, 50);
  RUN_TEST(test_SubFullOflow, 51);
  RUN_TEST(test_SubUsecNorm, 52);
  RUN_TEST(test_SubUsecOflow, 53);
  RUN_TEST(test_Neg, 54);
  RUN_TEST(test_AbsNoFrac, 55);
  RUN_TEST(test_AbsWithFrac, 56);
  RUN_TEST(test_Helpers2, 57);
  RUN_TEST(test_ToLFPbittest, 58);
  RUN_TEST(test_ToLFPrelPos, 59);
  RUN_TEST(test_ToLFPrelNeg, 60);
  RUN_TEST(test_ToLFPabs, 61);
  RUN_TEST(test_FromLFPbittest, 62);
  RUN_TEST(test_FromLFPrelPos, 63);
  RUN_TEST(test_FromLFPrelNeg, 64);
  RUN_TEST(test_LFProundtrip, 65);
  RUN_TEST(test_ToString, 66);

  return (UnityEnd());
}
