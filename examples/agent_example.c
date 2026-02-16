// agent_example.c - Example usage of CClaw Agent framework
// SPDX-License-Identifier: MIT

#include "cclaw.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    printf("CClaw Agent Framework Example\n");
    printf("=============================\n\n");

    // Initialize CClaw
    err_t err = cclaw_init();
    if (err != ERR_OK) {
        fprintf(stderr, "Failed to initialize: %s\n", error_to_string(err));
        return 1;
    }

    // Load configuration
    config_t* config = NULL;
    err = config_load(STR_NULL, &config);
    if (err != ERR_OK) {
        fprintf(stderr, "Failed to load config: %s\n", error_to_string(err));
        cclaw_shutdown();
        return 1;
    }

    // Create agent with default configuration
    agent_config_t agent_config = agent_config_default();
    agent_config.enable_summarization = true;
    agent_config.max_iterations = 10;

    agent_t* agent = NULL;
    err = agent_create(&agent_config, &agent);
    if (err != ERR_OK) {
        fprintf(stderr, "Failed to create agent: %s\n", error_to_string(err));
        config_destroy(config);
        cclaw_shutdown();
        return 1;
    }

    printf("Agent created successfully!\n");
    printf("  Max iterations: %u\n", agent_config.max_iterations);
    printf("  Autonomy level: %s\n",
        agent_config.autonomy_level == AUTONOMY_LEVEL_READONLY ? "readonly" :
        agent_config.autonomy_level == AUTONOMY_LEVEL_SUPERVISED ? "supervised" : "full");

    // Create a new session
    str_t session_name = STR_LIT("example_session");
    agent_session_t* session = NULL;

    err = agent_session_create(agent, &session_name, &session);
    if (err != ERR_OK) {
        fprintf(stderr, "Failed to create session: %s\n", error_to_string(err));
        agent_destroy(agent);
        config_destroy(config);
        cclaw_shutdown();
        return 1;
    }

    printf("\nSession created: %.*s\n",
           (int)session->id.len, session->id.data);

    // Example: Process a message (would need a configured provider)
    printf("\nNote: This example requires a configured LLM provider.\n");
    printf("Run 'cclaw onboard' to set up your API key.\n");

    // Demonstrate Pi-style conversation tree
    printf("\n--- Pi-Style Conversation Tree Demo ---\n");

    // Create user message
    agent_message_t* user_msg = agent_message_create(
        AGENT_MSG_USER,
        &STR_LIT("Hello, can you help me with a coding task?")
    );

    // Add to session
    agent_message_add_child(session->root ? session->root : user_msg, user_msg);
    if (!session->root) session->root = user_msg;
    session->current = user_msg;

    printf("User message added to tree.\n");
    printf("  Message ID: %.*s\n", (int)user_msg->id.len, user_msg->id.data);
    printf("  Content: %.*s\n", (int)user_msg->content.len, user_msg->content.data);

    // Simulate assistant response
    agent_message_t* assistant_msg = agent_message_create(
        AGENT_MSG_ASSISTANT,
        &STR_LIT("I'd be happy to help! What would you like to work on?")
    );

    agent_message_add_child(user_msg, assistant_msg);
    session->current = assistant_msg;

    printf("\nAssistant response added.\n");

    // Demonstrate branching
    printf("\n--- Branching Demo ---\n");

    agent_message_t* branch;
    err = agent_create_branch(agent, user_msg, &branch);
    if (err == ERR_OK) {
        printf("Created a new branch from user message.\n");
        printf("Branch ID: %.*s\n", (int)branch->id.len, branch->id.data);
    }

    // Demonstrate extension concept
    printf("\n--- Extension System Demo (Pi Philosophy) ---\n");

    extension_registry_init();

    // Generate a tool extension
    str_t extension_source = STR_NULL;
    err = extension_generate_tool(
        &STR_LIT("calculator"),
        &STR_LIT("A simple calculator tool"),
        &STR_LIT("{\"type\": \"object\", \"properties\": {\"expression\": {\"type\": \"string\"}}}"),
        &STR_LIT("    // Parse expression and calculate result\n    // For now, just return the args\n    tool_result_set_success(result, args);"),
        &extension_source
    );

    if (err == ERR_OK) {
        printf("Generated tool extension source code:\n");
        printf("---\n%.*s\n---\n", (int)extension_source.len, extension_source.data);
        free((void*)extension_source.data);
    }

    extension_registry_shutdown();

    // Cleanup
    printf("\n--- Cleanup ---\n");
    agent_session_close(agent, session);
    agent_destroy(agent);
    config_destroy(config);
    cclaw_shutdown();

    printf("Example completed successfully!\n");
    return 0;
}
