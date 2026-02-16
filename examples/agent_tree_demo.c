// agent_tree_demo.c - Demonstrates Pi-style conversation tree
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cclaw.h"

// Simple UUID generation for demo
static void generate_id(char* out, size_t len) {
    const char* chars = "0123456789abcdef";
    for (size_t i = 0; i < len && i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            out[i] = '-';
        } else {
            out[i] = chars[rand() % 16];
        }
    }
    out[len > 36 ? 36 : len] = '\0';
}

// Print conversation tree recursively
void print_tree(agent_message_t* node, int depth, const char* prefix) {
    if (!node) return;

    // Print current node
    const char* type_str = "?";
    switch (node->type) {
        case AGENT_MSG_USER: type_str = "U"; break;
        case AGENT_MSG_ASSISTANT: type_str = "A"; break;
        case AGENT_MSG_TOOL_CALL: type_str = "T"; break;
        case AGENT_MSG_TOOL_RESULT: type_str = "R"; break;
        case AGENT_MSG_SYSTEM: type_str = "S"; break;
        case AGENT_MSG_SUMMARY: type_str = "M"; break;
    }

    // Truncate content for display
    char content[60] = {0};
    int content_len = node->content.len < 50 ? node->content.len : 50;
    if (node->content.data) {
        strncpy(content, node->content.data, content_len);
        if (node->content.len > 50) strcat(content, "...");
    }

    printf("%s%s[%s] %s\n", prefix, depth == 0 ? "┌─ " : "├─ ", type_str, content);

    // Print children
    for (uint32_t i = 0; i < node->child_count; i++) {
        char new_prefix[256];
        snprintf(new_prefix, sizeof(new_prefix), "%s%s", prefix, "│  ");
        print_tree(node->children[i], depth + 1, new_prefix);
    }
}

int main(void) {
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║     CClaw Agent - Pi-Style Conversation Tree Demo        ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n\n");

    // Initialize
    srand((unsigned)time(NULL));

    printf("Creating conversation tree...\n\n");

    // Simulate a conversation
    // Root: System prompt
    agent_message_t* root = calloc(1, sizeof(agent_message_t));
    root->type = AGENT_MSG_SYSTEM;
    root->content.data = strdup("You are a helpful coding assistant.");
    root->content.len = strlen(root->content.data);

    // User asks about refactoring
    agent_message_t* user_msg1 = calloc(1, sizeof(agent_message_t));
    user_msg1->type = AGENT_MSG_USER;
    user_msg1->content.data = strdup("Help me refactor this Python function");
    user_msg1->content.len = strlen(user_msg1->content.data);
    user_msg1->parent = root;
    agent_message_add_child(root, user_msg1);

    // Assistant responds
    agent_message_t* assistant_msg1 = calloc(1, sizeof(agent_message_t));
    assistant_msg1->type = AGENT_MSG_ASSISTANT;
    assistant_msg1->content.data = strdup("I'd be happy to help! Please share the function.");
    assistant_msg1->content.len = strlen(assistant_msg1->content.data);
    assistant_msg1->parent = user_msg1;
    agent_message_add_child(user_msg1, assistant_msg1);

    // User shares code
    agent_message_t* user_msg2 = calloc(1, sizeof(agent_message_t));
    user_msg2->type = AGENT_MSG_USER;
    user_msg2->content.data = strdup("def calc(x): return x*x");
    user_msg2->content.len = strlen(user_msg2->content.data);
    user_msg2->parent = assistant_msg1;
    agent_message_add_child(assistant_msg1, user_msg2);

    // Assistant suggests refactoring
    agent_message_t* assistant_msg2 = calloc(1, sizeof(agent_message_t));
    assistant_msg2->type = AGENT_MSG_ASSISTANT;
    assistant_msg2->content.data = strdup("Here's a refactored version with type hints...");
    assistant_msg2->content.len = strlen(assistant_msg2->content.data);
    assistant_msg2->parent = user_msg2;
    agent_message_add_child(user_msg2, assistant_msg2);

    // ============================================
    // BRANCH 1: Alternative refactoring approach
    // ============================================
    printf("┌─ Main Conversation ────────────────────────────────────┐\n");
    print_tree(root, 0, "");
    printf("└────────────────────────────────────────────────────────┘\n\n");

    // Create a branch (Pi-style)
    printf("Creating alternative branch...\n\n");

    agent_message_t* branch_point = user_msg2; // Branch from code sharing

    // Alternative assistant response (branch)
    agent_message_t* branch_assistant = calloc(1, sizeof(agent_message_t));
    branch_assistant->type = AGENT_MSG_ASSISTANT;
    branch_assistant->content.data = strdup("[Branch] Let's use a class-based approach instead...");
    branch_assistant->content.len = strlen(branch_assistant->content.data);
    branch_assistant->parent = branch_point;

    // Add as sibling to the original response
    agent_message_add_child(branch_point, branch_assistant);

    // Continue the branch
    agent_message_t* branch_user = calloc(1, sizeof(agent_message_t));
    branch_user->type = AGENT_MSG_USER;
    branch_user->content.data = strdup("That looks more complex, why?");
    branch_user->content.len = strlen(branch_user->content.data);
    branch_user->parent = branch_assistant;
    agent_message_add_child(branch_assistant, branch_user);

    printf("┌─ After Branching ──────────────────────────────────────┐\n");
    print_tree(root, 0, "");
    printf("└────────────────────────────────────────────────────────┘\n\n");

    // ============================================
    // Demonstrate navigation
    // ============================================
    printf("Navigation demo:\n");
    printf("  - Branch 1 (original): %p\n", (void*)assistant_msg2);
    printf("  - Branch 2 (alternative): %p\n", (void*)branch_assistant);
    printf("  - Branch point: %p\n\n", (void*)branch_point);

    printf("Pi-style tree features demonstrated:\n");
    printf("  ✓ Non-linear conversation (branching)\n");
    printf("  ✓ Multiple response paths\n");
    printf("  ✓ Parent-child relationships\n");
    printf("  ✓ Sibling navigation\n");
    printf("  ✓ Conversation history preservation\n\n");

    // Cleanup
    printf("Cleaning up...\n");
    agent_message_tree_free(root);

    printf("\nDemo complete!\n");
    return 0;
}
