



i=0


while [ $i -lt 50000 ]
do
/letv/dls/ae/client 117.121.54.72 "aaaaaaaaaaaaaaaaaGGGGGG" 7878 &
#./c 127.0.0.1 "aaaaaaaaaaaaaaaaaGGGGGG" 7878
((i++))
done

