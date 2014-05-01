#!/bin/sh
for file in $(ls /bin/)
do
  if [[ $(cat /bin/$file | head -n 1 | cut -b2-4) == 'ELF' ]]
  then
    echo
    echo $file
    ./bind stub_exit_43 /bin/$file 2 > tests/$file
    chmod +x tests/$file
  fi
done
