/* 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <keyutils.h>
#include <assert.h>

#include "str-replace.h"
#include "opts.h"

#define SM_MAGIC 0x534d5f4d

#define KEY_PAYLOAD_MAXLEN 512
#define SESSION_NAME_MAXLEN 512


void
print_usage() {
	fprintf(stderr, "USAGE:\n");
	fprintf(stderr, "  secret-manager  [-h | -q | -r] [CMD [ARGS...]] \n");
	fprintf(stderr, "    -h   Show this help. \n");
	fprintf(stderr, "    -q   Quit the keyring session. \n");
	fprintf(stderr, "    -r   Specify the replace string. \n");
}

int
main(int argc, char *argv[])
{
	int ret = 1;
	char *secret_cmd = NULL;
	struct sm_opts *opts = NULL;

	key_serial_t key_id; 
	char key_payload[KEY_PAYLOAD_MAXLEN];
	char session_name[SESSION_NAME_MAXLEN];

	// parse_opts gives use default values if not provided
	opts = sm_opts_parse(argc, argv);
	if (opts == NULL) {
		ret = 1;
		goto exit;
	}

	if (opts->flags & OPT_HELP || opts->flags & OPT_UNRECOGNIZED) {
		print_usage();
		ret = 0;
		goto exit;
	}

	// We deal with only one session name for the moment
	sprintf(session_name, "sm-session-%u", SM_MAGIC);

	// Start to request a key in the current session if present
	key_id = request_key("user", session_name, NULL, KEY_SPEC_SESSION_KEYRING);

	if (opts->flags & OPT_QUIT ) {
		if (key_id > 0)
			keyctl_revoke(key_id);
		else
			fprintf(stderr, "No keyring session could be found.\n");
		ret = 0;
		goto exit;
	}

	if (key_id <= 0) {
		// Ask the user to enter his secret phrase and
		memset(key_payload, 0, KEY_PAYLOAD_MAXLEN);
		sprintf(key_payload, "%s", getpass("Secret: "));
		key_id = add_key("user", session_name, key_payload, 
				strlen(key_payload), KEY_SPEC_SESSION_KEYRING); 
		if (key_id < 0) {
			fprintf(stderr, "FATAL: Cannot add a passphrase, is CONFIG_KEYS enabled in your kernel?\n");
			ret = 1;
			goto exit;
		}
		// To be able to find again the passphrase in the next execution of sm,
		// we have to attach the current keyring session to the shell
		// interpreter which executed this cmd
		keyctl_session_to_parent();
	} else {
		// Read the passphrase
		memset(key_payload, 0, KEY_PAYLOAD_MAXLEN);
		if (keyctl_read(key_id, key_payload, KEY_PAYLOAD_MAXLEN) < 0) {
			ret = 1;
			goto exit;
		}
	}

	// Replace all {} occurrences by the secret phrase
	secret_cmd = replace_str(opts->cmd, opts->repl_str, key_payload);

	// Execute the secret cmd
	if (secret_cmd) {
		system(secret_cmd);
		free(secret_cmd);
	}

exit:
	if (opts)
		sm_opts_free(opts);

	return ret;
}

