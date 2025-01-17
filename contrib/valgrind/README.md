# Valgrind

The [Valgrind](https://valgrind.org/) tool suite provides a number of debugging and profiling tools that help you make your programs faster and more correct. The most popular and the default of these tools is called **Memcheck**. It can detect many memory-related errors that can lead to crashes and unpredictable behaviour.

# Run memory management detection

``` bash
valgrind --leak-check=full --show-leak-kinds=all --log-file=%p.log --trace-children=yes --track-origins=yes --read-var-info=yes ./pgagroal -c pgagroal.conf -a pgagroal_hba.conf
```

# Use suppressions rules

``` bash
valgrind --suppressions=../../contrib/valgrind/pgagroal.supp --leak-check=full --show-leak-kinds=all --log-file=%p.log --trace-children=yes --track-origins=yes --read-var-info=yes ./pgagroal -c pgagroal.conf -a pgagroal_hba.conf
```

# Generate suppressions rules

``` bash
valgrind --gen-suppressions=all --leak-check=full --show-leak-kinds=all --log-file=%p.log --trace-children=yes --track-origins=yes --read-var-info=yes ./pgagroal -c pgagroal.conf -a pgagroal_hba.conf
```

