# cmake -DIN=FILE.gz -DOUT=FILE -P gunzip.cmake: a gzipped test fixture, unpacked
execute_process(COMMAND gzip -dc ${IN} OUTPUT_FILE ${OUT} RESULT_VARIABLE rc)
if(rc)
    file(REMOVE ${OUT})
    message(FATAL_ERROR "gzip -dc ${IN}: ${rc}")
endif()
