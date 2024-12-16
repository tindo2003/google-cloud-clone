#ifndef EMAIL_API_H
#define EMAIL_API_H
#include "storage_request.h"
#include "storage_api.h"
#include <stdbool.h>
#include <stddef.h>
#include "cache.h"
#include <string>
#include <iostream>
#include <openssl/md5.h>

// Function to get mailbox information.
// Parameters:
// - user_id: The user ID whose mailbox is being accessed.
// - mailbox_name: The name of the mailbox (e.g., inbox, outbox).
// - status_code: A reference to store the operation's status code.
// Returns a char pointer containing the mailbox data.
std::string get_mailbox_or_email(std::string user_id, std::string mailbox_name, std::string status_code);

// Function to store mailbox information.
// Parameters:
// - user_id: The user ID whose mailbox is being updated.
// - mailbox_name: The name of the mailbox to update.
// - metadata: Metadata related to the email.
// - uuid: The UUID of the email.
// - email: The email contents.
// - status_code: A reference to store the operation's status code.
// Returns true if the operation is successful, false otherwise.
bool put_mailbox(std::string user_id, std::string mailbox_name, std::string metadata, std::string uuid, std::string email, std::string status_code, bool mail_only);

bool delete_email(std::string user_id, std::string mailbox_name, std::string uuid, std::string status_code, bool meta_only);

void computeDigest(char *data, int dataLengthBytes, unsigned char *digestBuffer);


std::string compute_UUID(std::string meta);


std::string assemble_mailbox(const std::string& metadataString);


std::string assemble_single_email(std::string sender, std::string recipient, std::string timestamp, std::string message);

std::string assemble_email_display(std::string uuid, std::string target, std::string subject, std::string message);



bool send_external(std::string address, std::string uuid, std::string message, std::string sender);
#endif
