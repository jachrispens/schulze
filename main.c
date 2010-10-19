#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

void print_usage_and_die(char *program_name);
bool determine_winners(FILE *rankings, int candidate_count, bool winners[candidate_count]);
int max(int a, int b);
int min(int a, int b);
bool parse_votes(FILE *file, int candidate_count, int votes[candidate_count][candidate_count]);
void tally(int candidate_count, int votes[candidate_count][candidate_count],
	   int candidate_order[candidate_count], int rankings[candidate_count]);
void print_graph_matrix(int n, int graph[n][n]);
void print_array(int n, int array[n]);

enum {MAX_CANDIDATES = 256};

int main(int argc, char **argv)
{

  if (argc == 0) {
    fputs("usage: -c <candidate count> [ranking filename]\n", stderr);
    exit(1);
  }

  char *program_name = *argv++;
  int count = 0;
  char *ranking_filename = NULL;

  for ( ; *argv != NULL; argv++) {
    if (strcmp(*argv, "-c") == 0) {
      if (*++argv == NULL) {
	print_usage_and_die(program_name);
      }
      char *end = NULL;
      long int number = strtol(*argv, &end, 10);
      if (end[0] != '\0' || *argv == end) {
	print_usage_and_die(program_name);
      }
      if (number < 1 || number > MAX_CANDIDATES) {
	fprintf(stderr, "candidate count of %ld is out of range!\n", number);
	print_usage_and_die(program_name);
      }
      count = number;
    } else {
      if (ranking_filename != NULL) {
	print_usage_and_die(program_name);
      }
      ranking_filename = *argv;
    }
  }

  if (count == 0) {
    fputs("must provide candidate count with -c\n", stderr);
    print_usage_and_die(program_name);
  }

  FILE *votes = NULL;
  if (ranking_filename != NULL) {
    votes = fopen(ranking_filename, "rb");
    if (votes == NULL) {
      fprintf(stderr, "ERROR: could not open votes file, %s!\n", ranking_filename);
      return 1;
    }
  } else {
    votes = stdin;
  }

  bool winners[count];
  if (determine_winners(votes, count, winners)) {
    for (int index = 0; index < count; index++) {
      if (winners[index] == true) {
	printf("winner: candidate %d\n", index + 1);
      }
    }
  }

  fclose(votes);

  return 0;
}

/*
 * print_usage_and_die does just that.
 */
void print_usage_and_die(char *program_name)
{
  fprintf(stderr, "usage: %s -c <candidate_count> [ranking filename]\n", program_name);
  exit(1);
}
	  


/*
 * detemine_winners reads the ranking file and finds the winning
 * candidates (there may be more than one) by Schulze's method).  True
 * is returned on success; false on failure.
 */
bool determine_winners(FILE *rankings, int candidate_count, bool winners[candidate_count])
{
  int graph[candidate_count][candidate_count];

  for (int row = 0; row < candidate_count; row++) {
    for (int column = 0; column < candidate_count; column++) {
      graph[row][column] = 0;
    }
  } 

  if (!parse_votes(rankings, candidate_count, graph)) {
    return false;
  }
  puts("tallied votes:");
  print_graph_matrix(candidate_count, graph);

  /* find the pairwise victors using the number of winner votes as the
     strength */
  for (int row = 0; row < candidate_count; row++) {
    for (int column = 0; column < row; column++) {
      int support_for = graph[row][column];
      int opposition_against = graph[column][row];
      if (support_for > opposition_against) {
	graph[column][row] = 0;
      } else if (support_for < opposition_against) {
	graph[row][column] = 0;
      } else {
	graph[row][column] = graph[column][row] = 0;
      }
    }
  }

  /* find the strongest paths */
  for (int intermediary = 0; intermediary < candidate_count; intermediary++) {
    for (int row = 0; row < candidate_count; row++) {
      if (intermediary != row) {
	for (int column = 0; column < candidate_count; column++) {
	  if (intermediary != column && row != column) {
	    graph[row][column] = max(graph[row][column], 
				     min(graph[row][intermediary],
					 graph[intermediary][column]));
	  }
	}
      }
    }
  }
  puts("strongest paths:");
  print_graph_matrix(candidate_count, graph);
  
  /* find the winners */
  for (int row = 0; row < candidate_count; row++) {
    winners[row] = true;
    for (int column = 0; column < candidate_count; column++) {
      if (row != column) {
	if (graph[column][row] > graph[row][column]) {
	  winners[row] = false;
	}
      }
    }
  }

  return true; 
}

/* 
 * returns the maximum value between a and b
 */
inline int max(int a, int b)
{
  return a > b ? a : b;
}

/*
 * returns the minimum value between a and b
 */
inline int min(int a, int b) {
  return a < b ? a : b;
}

/*
 * parse_votes parses votes from file.  The format of a vote is
 * expressed by the following regular expression: (\r|\n)*(
 * |\t)*[0-9]+( \t)*((>|=)( |\t)*[0-9]+)*( \t)*(\r|\n).  The parsing is
 * implemented by a state machine, which is hopefully obvious given
 * the RE.  parse_votes returns 0 on success and a negative value on
 * failure.
 */
bool parse_votes(FILE *file, int candidate_count, int votes[candidate_count][candidate_count])
{
  /* current_character */
  int c;

  /* tracks which candidates have been seen in the current vote. */
  bool candidates[candidate_count];

  /* tracks the order in which candidates have been seen. */
  int candidate_order[candidate_count];
  int candidate_order_index = 0;

  /* tracks the partitions within the candidate_order array; i.e. the
     divisions between preferred and equivalent candidates */
  int ranking[candidate_count];
  int *current_ranking = ranking;

  enum {START, EMPTY_INPUT, BEFORE_NUMBER, NUMBER, 
	AFTER_NUMBER, GREATER_THAN, EQUAL, 
	END_LINE, END_PARSING} state = START;

  while (true) {
    switch (state) {
    case START:
      /* initialize arrays to accept the incoming vote. */
      for (int index = 0; index < candidate_count; index++) {
        candidate_order[index] = ranking[index] = 0;
	candidates[index] = false;
      }
      candidate_order_index = 0;
      current_ranking = ranking;

      c = fgetc(file);
      if (isdigit(c)) {
	state = NUMBER;
	break;
      }
      switch (c) {
      case '\r': case '\n':
      case ' ': case '\t':
	state = EMPTY_INPUT;
	break;
      case EOF:
	return true;
	break;
      default:
	goto unexpected_input;
	break;
      }
      break;

    case EMPTY_INPUT:
      c = fgetc(file);
      if (isdigit(c)) {
	state = NUMBER;
	break;
      }
      switch (c) {
      case '\r': case '\n':
      case ' ': case '\t':
	break;
      case EOF:
	return true;
	break;
      default:
	goto unexpected_input;
	break;
      }
      break;

    case BEFORE_NUMBER:
      c = fgetc(file);
      if (isdigit(c)) {
	state = NUMBER;
	break;
      }
      if (c == ' ' || c == '\t') {
	break;
      } else {
	goto unexpected_input;
      }
      break;

    case NUMBER:
      candidate_order[candidate_order_index] = candidate_order[candidate_order_index] * 10 + (c - '0');

      c = fgetc(file);
      if (isdigit(c)) {
	break;
      }
      /* before we leave the number state, make sure that number makes
	 sense; additionally, make it 0 indexed.  Also, update the index */
      int candidate = --candidate_order[candidate_order_index];
      if (candidate < 0 || candidate >= candidate_count) {
	fprintf(stderr, "ERROR: candidate %d is invalid! (1, %d)\n", candidate + 1, candidate_count);
	return false;
      }
      if (candidates[candidate] == false) {
	candidates[candidate] = true;
      } else {
	fprintf(stderr, "ERROR: candidate %d is ranked twice!\n", candidate + 1);
	return false;
      }
      candidate_order_index++;

      switch (c) {
      case ' ': case '\t':
	state = AFTER_NUMBER;
	break;
      case '\r': case '\n':
	state = END_LINE;
	break;
      case EOF:
	state = END_PARSING;
	break;
      case '>':
	state = GREATER_THAN;
	break;
      case '=':
	state = EQUAL;
	break;
      default:
	goto unexpected_input;
	break;
      }
      break;
      
    case AFTER_NUMBER:
      c = fgetc(file);
      switch (c) {
      case '\r': case '\n':
	state = END_LINE;
	break;
      case EOF:
	state = END_PARSING;
	break;
      case ' ': case '\t':
	break;
      case '>':
	state = GREATER_THAN;
	break;
      case '=':
	state = EQUAL;
	break;
      default:
	goto unexpected_input;
	break;
      }
      break;

      /* the GREATER_THAN and EQUAL states overlap */
    case GREATER_THAN:
      *current_ranking++ = candidate_order_index;
    case EQUAL:
      c = fgetc(file);
      if (isdigit(c)) {
	state = NUMBER;
	break;
      }
      if (c == ' ' || c == '\t') {
	state = BEFORE_NUMBER;
	break;
      }
      break;

      /* the END_LINE and END_PARSING states also have a large amount
	 of overlap */
    case END_LINE:
    case END_PARSING:
      /* if there are unspecified candidates, add them to the overall
	 order as being the least preferred alternatives */
      if (candidate_order_index < candidate_count) {
	*current_ranking = candidate_order_index;
	for (int index = 0; index < candidate_count; index++) {
	  if (candidates[index] == false) {
	    candidate_order[candidate_order_index++] = index;
	  }
	}
      }

      tally(candidate_count, votes, candidate_order, ranking);
     
      if (state == END_LINE) {
	state = START;
	break;
      } else {
	return true;
	break;
      }
      
    default:
      fputs("ERROR: interal parser error; unrecognized state\n", stderr);
      return false;
    }
  }

 unexpected_input:
  if (c == EOF) {
    fputs("ERROR: premature end of input\n", stderr);
  } else if (isprint(c)) {
    fprintf(stderr, "ERROR: unexpcted character in input: %c\n", c);
  } else {
    fputs("ERROR: unexpected non-printable input\n", stderr);
  }
  return false;
}

/*
 * tally tallies a vote.  
 *
 * A vote in this case is an ordering of groups of candidates by
 * preference.  
 *
 * As such, the candidate_order array contains the candidates ordered
 * by preference (higher preference first).
 *
 * The ranking array contains the partitioning; an element n in the
 * ranking array indicates that candidates such that
 * candidate_order[x] (x < n) are preferred to candidates where
 * candidate_order[y] (y >= n).  The elements of ranking must be
 * strictly increasing (this is not checked).  A 0 value indicates the
 * end of the partitioning.
 */
void tally(int candidate_count, int votes[candidate_count][candidate_count], 
	   int candidate_order[candidate_count], int ranking[candidate_count])
{
  int partition_start = 0;
  int partition_location = *ranking++;

  while (partition_location != 0) {
    for (int lefthand_element = partition_start; 
	 lefthand_element < partition_location; 
	 lefthand_element++) {
      for (int righthand_element = partition_location; 
	   righthand_element < candidate_count; 
	   righthand_element++) {
	votes[candidate_order[lefthand_element]][candidate_order[righthand_element]]++;
      }
    }
    partition_start = partition_location;
    partition_location = *ranking++;
  }
}



void print_graph_matrix(int n, int graph[n][n]) 
{
  printf("     ");
  for (int i = 1; i <= n; i++) {
    printf("%5d ", i);
  }
  puts("");

  for (int row = 0; row < n; row++) {
    printf("%3d: ", row + 1);
    for (int column = 0; column < n; column++) {
      printf("%5d ", graph[row][column]);
    }
    puts("");
  }
  puts("");
}

void print_array(int n, int array[n])
{
  for (int index = 0; index < n; index++) {
    printf("%4d ", array[index]);
  }
  puts("");
}

