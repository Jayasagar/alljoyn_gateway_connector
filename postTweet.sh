#!/bin/sh
# Copyright (c) 2014, AllSeen Alliance. All rights reserved.
#
#    Permission to use, copy, modify, and/or distribute this software for any
#    purpose with or without fee is hereby granted, provided that the above
#    copyright notice and this permission notice appear in all copies.
#
#    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
#    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
#    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
#    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
#    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
#    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
#    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#

##################################################################
### Function to url urlEncode a string
##################################################################
urlEncode() {
  local input="$*"
  local strlen=${#input}
  local encoded=""
  local i=1

  while [ $i -lt $((strlen+1)) ]; do
     char=$(expr substr "$input" "$i" 1)
     case "$char" in
        [-_.~a-zA-Z0-9] ) enc="${char}" ;;
        * )               enc=$(printf '%%%02X' "'$char")
     esac
     encoded="${encoded}${enc}"
     i=$((i+1))
  done
  echo "${encoded}"
}

DEBUG=$(env | awk /DEBUG=/ | sed 's/DEBUG=//')
if [ "$DEBUG" ]; then
    echo "Debug mode is set. Printing parameter values"
    echo ""
fi

##################################################################
### Values necessary to Send Authenticated Requests with Twitter
### The first 4 oauth parameters need to be filled with the information for the relevant account
##################################################################

oauth_consumer_key='TO_BE_FILLED'
oauth_consumer_secret='TO_BE_FILLED'
oauth_token='TO_BE_FILLED'
oauth_secret='TO_BE_FILLED'

oauth_signature_method='HMAC-SHA1'
oauth_version='1.0'
request_type='POST'
request_url='https://api.twitter.com/1.1/statuses/update.json'

oauth_consumer_secret_ec=$(urlEncode ${oauth_consumer_secret})
oauth_secret_ec=$(urlEncode ${oauth_secret})
oauth_consumer_key_ec=$(urlEncode ${oauth_consumer_key})
oauth_signature_method_ec=$(urlEncode ${oauth_signature_method})
oauth_token_ec=$(urlEncode ${oauth_token})
oauth_version_ec=$(urlEncode ${oauth_version})
request_type_ec=$(urlEncode ${request_type})
request_url_ec=$(urlEncode ${request_url})

if [ "$DEBUG" ]; then 
	echo "Authentication keys:"
	printf "%-25s: %s\n" oauth_consumer_key 	 $oauth_consumer_key
	printf "%-25s: %s\n" oauth_token 			 $oauth_token
	printf "%-25s: %s\n" oauth_consumer_secret 	 $oauth_consumer_secret
	printf "%-25s: %s\n" oauth_secret 			 $oauth_secret
	printf "%-25s: %s\n" oauth_signature_method  $oauth_signature_method
	printf "%-25s: %s\n" oauth_version           $oauth_version
	printf "%-25s: %s\n" request_type 			 $request_type
	printf "%-25s: %s\n" request_url 			 $request_url
	echo ""
fi

##################################################################
### Random keys that need to be unique per request
##################################################################

oauth_timestamp=$(date +%s)
oauth_nonce=$(date +%s%T123456789 | openssl base64 | sed -e s'/[+=/]//g')

oauth_nonce_ec=$(urlEncode ${oauth_nonce})
oauth_timestamp_ec=$(urlEncode ${oauth_timestamp})

if [ "$DEBUG" ]; then 
	echo "Random Generated keys:";
	printf "%-25s: %s\n" oauth_timestamp 	$oauth_timestamp
	printf "%-25s: %s\n" oauth_nonce 		$oauth_nonce
	echo ""
fi

##################################################################
### Parameters (status) for the call to Twitter
### If no params are given use default parameters
### Note that the same tweet can not be sent twice consecutively so we add a timestamp
##################################################################

status="Testing Sending Tweet With Twitter at timestamp ${oauth_timestamp}"
if [ "$#" -eq 1 ]; then
	status="${1}"
fi

status_ec=$(urlEncode ${status})

if [ "$DEBUG" ]; then 
	echo "Call Parameters:"
	printf "%-25s: %s\n" status  	"$status"
	echo ""
fi

##################################################################
### Generating oauth Signature - unique per request
### For more information on how to generate the signature see Twitter Documentation:
### https://dev.twitter.com/docs/auth/creating-signature
##################################################################

signature_key="$oauth_consumer_secret_ec&$oauth_secret_ec"
signature_param_string="\
oauth_consumer_key=$oauth_consumer_key_ec&\
oauth_nonce=$oauth_nonce_ec&\
oauth_signature_method=$oauth_signature_method_ec&\
oauth_timestamp=$oauth_timestamp_ec&\
oauth_token=$oauth_token_ec&\
oauth_version=$oauth_version_ec&\
status=$status_ec"
signature_param_string_ec=$(urlEncode ${signature_param_string})

signature_value_string="$request_type_ec&$request_url_ec&$signature_param_string_ec"
oauth_signature=`echo -n ${signature_value_string} | openssl dgst -sha1 -hmac ${signature_key} -binary | openssl base64`
oauth_signature_ec=$(urlEncode ${oauth_signature})

if [ "$DEBUG" ]; then 
	echo "Generating Signature:"
	printf "%-25s: %s\n" signature_key 			 $signature_key
	printf "%-25s: %s\n" signature_param_string  $signature_param_string
	printf "%-25s: %s\n" signature_value_string  $signature_value_string
	printf "%-25s: %s\n" oauth_signature 		 $oauth_signature
	echo ""
fi

##################################################################
### Call to Twitter
### Please note that tweeting is limited to 250 messages a day
### It is possible that they are limited to X amount per hour as well. 
##################################################################

oauthHeader="OAuth \
oauth_consumer_key=\"$oauth_consumer_key_ec\", \
oauth_nonce=\"$oauth_nonce_ec\", \
oauth_signature=\"$oauth_signature_ec\", \
oauth_signature_method=\"$oauth_signature_method_ec\", \
oauth_timestamp=\"$oauth_timestamp_ec\", \
oauth_token=\"$oauth_token_ec\", \
oauth_version=\"$oauth_version_ec\""
encodingHeader="application/x-www-form-urlencoded"
data="status=$status_ec"

if [ "$DEBUG" ]; then 
        printf "%-25s: %s\n" oauthHeader $oauthHeader
        printf "%-25s: %s\n" data $data
	echo ""
fi
                
result=`curl -k --request "$request_type" "$request_url" --data $data --header "Content-Type: ${encodingHeader}" --header "Authorization: ${oauthHeader}"` 

echo "$result"
