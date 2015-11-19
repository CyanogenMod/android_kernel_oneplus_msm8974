#!/bin/sh

sdate=${1}
cdate=`date +"%m_%d_%Y"`
rdir=`pwd`

rm -rf Changelog_*

clear

# Check the date start range is set
if [ -z "$sdate" ]; then
echo""
echo "ATTENTION: Start date not defined ----------------------------------------------------"
    echo""
    echo " >>> Please define a start date in mm/dd/yyyy format ..."
    echo""
    echo "----------------------------------------------------------------------------------"
    read sdate
fi

# Find the directories to log
echo"";echo"";echo""
echo "AK CHANGELOG -------------------------------------------------------------------------"
echo""
find $rdir -name .git | sed 's/\/.git//g' | sed 'N;$!P;$!D;$d' | while read line
do
cd $line
    # Test to see if the repo needs to have a changelog written.
    log=$(git log --pretty="%an - %s" --no-merges --since=$sdate --date-order)
    project="AK KERNEL"
    if [ -z "$log" ]; then
    echo " >>> Nothing updated on $project changelog, skipping ..."
    else
        # Write the changelog
        echo " >>> Changelog is updated and written for $project ..."
        echo "Project name: $project" >> "$rdir"/Changelog_$cdate.log
        echo "$log" | while read line
        do
echo " $line" >> "$rdir"/Changelog_$cdate.log
        done
echo "" >> "$rdir"/Changelog_$cdate.log
    fi
done
echo""
echo "------------------------------------------------------------------------------------"
echo"";echo"";echo""
