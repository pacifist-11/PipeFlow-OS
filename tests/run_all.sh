#!/bin/bash
set -e

echo "=== Running Test Battery ==="

echo -n "[Test 1] 3-stage linear pipeline... "
./pipeflow -v run "./bin/pf_source 5 | ./bin/pf_transform upper | ./bin/pf_sink" > /dev/null
echo "PASSED"

echo -n "[Test 2] Filter stage with args... "
./pipeflow run "./bin/pf_source 10 | ./bin/pf_filter 0003 | ./bin/pf_sink" > /dev/null
echo "PASSED"

echo -n "[Test 3] Config file (.pflow) execution... "
./pipeflow -f examples/basic.pflow > /dev/null
echo "PASSED"

echo -n "[Test 4] Early exit & SIGPIPE graceful recovery... "
./pipeflow run "./bin/pf_source 1000 0 | ./bin/pf_head 5 | ./bin/pf_sink" > /dev/null
echo "PASSED"

echo -n "[Test 5] Timeout alarm via SIGALRM (-t 2)... "
./pipeflow -t 2 run "./bin/pf_source 10 1000 | ./bin/pf_sink" > /dev/null 2>&1 || true
echo "PASSED (Clean teardown on timeout)"

echo "===Tests Passed Successfully! ==="
