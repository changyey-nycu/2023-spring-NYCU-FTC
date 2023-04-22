#include <iostream>
#include <fstream>
#include <string>
#include <stdlib.h>
#include <vector>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <sodium.h>
#include <pqxx/pqxx>

#include <grpc/grpc.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "eVoting.grpc.pb.h"

using google::protobuf::Timestamp;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using voting::AuthRequest;
using voting::AuthToken;
using voting::Challenge;
using voting::Election;
using voting::ElectionName;
using voting::ElectionResult;
using voting::eVoting;
using voting::Response;
// using voting::Status;
using voting::Vote;
using voting::VoteCount;
using voting::Voter;
using voting::VoterName;

#define DBCONNECTINFO "dbname=eVoting user=admin password=passwd hostaddr=127.0.0.1 port=8001"

pqxx::connection *dbConnect;

struct authStruct
{
  std::string name;
  std::string challenge;
  std::string token;
  std::time_t startTime;
};

// voter exist return 1, not exist return 0, error return 2
int checkVoterExist(std::string name)
{
  // check db exist the name
  pqxx::connection c(DBCONNECTINFO);
  pqxx::result r;
  pqxx::work tx(c);
  std::string sql("SELECT name FROM Voter WHERE name = '" + name + "';");

  try
  {
    r = tx.exec(sql);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    std::cerr << e.what() << '\n';
    return 2;
  }
  tx.commit();
  c.disconnect();

  if (r.size() != 0)
  {
    // std::cout << "has same voter name\n";
    return 1;
  }
  return 0;
}

// Election exist return 1, not exist return 0, error return 2
int checkElectionExist(std::string name)
{
  // check db exist the name
  pqxx::connection c(DBCONNECTINFO);
  pqxx::result r;
  pqxx::work tx(c);
  std::string sql("SELECT name FROM Election WHERE name = '" + name + "';");

  try
  {
    r = tx.exec(sql);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    std::cerr << e.what() << '\n';
    return 2;
  }
  tx.commit();
  c.disconnect();

  if (r.size() != 0)
  {
    // std::cout << "has same Election name\n";
    return 1;
  }
  return 0;
}

voting::Status RegisterVoter(Voter _voter)
{
  voting::Status vstatus;
  std::string name(_voter.name());

  std::string insert_val("'" + name + "', '" + _voter.group() + "'");
  std::string sql("INSERT INTO Voter(name , groups) VALUES (" + insert_val + ");");

  // check db exist the name
  int exist = checkVoterExist(name);
  if (exist == 0)
  {
    // add user to db
    pqxx::connection c(DBCONNECTINFO);
    pqxx::result r;
    pqxx::work tx(c);
    try
    {
      r = tx.exec(sql);
    }
    catch (const std::exception &e)
    {
      c.disconnect();
      std::cerr << e.what() << '\n';
      vstatus.set_code(2);
      return vstatus;
    }
    tx.commit();
    c.disconnect();
    vstatus.set_code(0);
    return vstatus;
  }
  else if (exist == 1)
  {
    vstatus.set_code(1);
    return vstatus;
  }

  // exist == 2 other error
  vstatus.set_code(2);
  return vstatus;
}

voting::Status UnregisterVoter(VoterName _voterName)
{
  voting::Status vstatus;
  std::string name = _voterName.name();
  // check db exist the name
  int exist = checkVoterExist(name);
  if (exist == 1)
  {
    // delete Voter from db by VoterName
    std::string insert_val("'" + name + "'");
    std::string sql("DELETE FROM Voter WHERE name = " + insert_val + ";");
    pqxx::connection c(DBCONNECTINFO);
    pqxx::result r;
    pqxx::work tx(c);
    try
    {
      r = tx.exec(sql);
    }
    catch (const std::exception &e)
    {
      c.disconnect();
      std::cerr << e.what() << '\n';
      vstatus.set_code(2);
      return vstatus;
    }
    tx.commit();
    c.disconnect();
    vstatus.set_code(0);
    return vstatus;
  }
  else if (exist == 0)
  {
    // No voter with the name exists on the server
    vstatus.set_code(1);
    return vstatus;
  }
  // exist == 2 other error
  vstatus.set_code(2);
  return vstatus;
}

class eVotingServer final : public eVoting::Service
{
public:
  explicit eVotingServer();
  Status PreAuth(ServerContext *context, const VoterName *voterName, Challenge *challenge);
  Status Auth(ServerContext *context, const AuthRequest *authRequest, AuthToken *authToken);
  Status CreateElection(ServerContext *context, const Election *election, voting::Status *status);
  Status CastVote(ServerContext *context, const Vote *vote, voting::Status *status);
  Status GetResult(ServerContext *context, const ElectionName *electionName, ElectionResult *electionResult);

private:
  int searchAuthByName(std::string _name);
  bool isAuth(std::string _token);
  std::vector<authStruct> userlist;
};

eVotingServer::eVotingServer()
{
}

int eVotingServer::searchAuthByName(std::string _name)
{
  for (int i = 0; i < userlist.size(); i++)
  {
    if (_name == userlist[i].name)
      return i;
  }
  return -1;
}

bool eVotingServer::isAuth(std::string _token)
{
  std::time_t nowtime = time(NULL);
  for (int i = 0; i < userlist.size(); i++)
  {
    if (_token == userlist[i].token)
    {
      if (nowtime - userlist[i].startTime > 3600)
      {
        userlist.erase(userlist.begin() + i);
        return false;
      }
      return true;
    }
  }
  return false;
}

Status eVotingServer::PreAuth(ServerContext *context, const VoterName *voterName, Challenge *challenge)
{
  std::string name = voterName->name();
  // use name to find db Voter
  int exist = checkVoterExist(name);
  if (exist == 0)
    return Status::CANCELLED;
  srand(time(NULL));
  int x = rand();
  std::string message(name + std::to_string(x));
  challenge->set_value(message);

  bool existlist = 0;
  authStruct temp;
  temp.name = name;
  temp.challenge = message;
  temp.startTime = time(NULL);
  for (int i = 0; i < userlist.size(); i++)
  {
    if (name == userlist[i].name)
    {
      userlist[i].challenge = message;
      userlist[i].startTime = time(NULL);
      existlist = 1;
    }
  }
  if (!existlist)
    userlist.push_back(temp);
  return Status::OK;
}

Status eVotingServer::Auth(ServerContext *context, const AuthRequest *authRequest, AuthToken *authToken)
{
  std::string restmp = authRequest->response().value();
  unsigned char *res = (unsigned char *)restmp.c_str();
  std::string _name = authRequest->name().name();
  std::string fileName("key/" + _name + "pk");
  int index = searchAuthByName(_name);
  if (index == -1)
    return Status::CANCELLED;
  char pktmp[crypto_sign_PUBLICKEYBYTES];

  // maybe replace by using DB
  std::fstream file;
  file.open(fileName, std::fstream::in | std::fstream::binary);
  if (file.is_open() == 0)
  {
    // std::cout << fileName << " pubilc key open file fail\n";
    return Status::CANCELLED;
  }
  file.read(pktmp, crypto_sign_PUBLICKEYBYTES);
  file.close();
  // end

  unsigned char *pk = (unsigned char *)pktmp;

  std::string mestmp = userlist[index].challenge;
  unsigned char *mes = (unsigned char *)mestmp.c_str();
  unsigned long long len = (unsigned long long)mestmp.length();
  if (crypto_sign_verify_detached(res, mes, len, pk) != 0)
  {
    std::cout << "verify sign fail\n";
  }
  else
  {
    userlist[index].startTime = time(NULL);
    srand(time(NULL));
    int x = rand();
    std::string passToken = std::to_string(x);
    int i = 0;
    while (i < userlist.size())
    {
      if (passToken == userlist[i].token)
      {
        srand(time(NULL));
        x = rand();
        passToken = std::to_string(x);
        i = 0;
      }
      i++;
    }

    userlist[index].token = passToken;
    authToken->set_value(passToken);
  }

  return Status::OK;
}

Status eVotingServer::CreateElection(ServerContext *context, const Election *election, voting::Status *status)
{
  // check token
  std::string token = election->token().value();
  if (!isAuth(token))
  {
    status->set_code(1);
    return Status::OK;
  }

  // Missing groups or choices specification
  if (election->groups_size() == 0 || election->choices_size() == 0)
  {
    status->set_code(2);
    return Status::OK;
  }
  std::string name = election->name();
  int exist = checkElectionExist(name);
  if (exist == 1)
  {
    std::cout << "same election name" << std::endl;
    status->set_code(3);
    return Status::OK;
  }

  std::string group_str = "";
  for (int i = 0; i < election->groups_size(); i++)
  {
    group_str += election->groups(i);
    group_str += " ";
  }
  // std::cout << "group_str:" << group_str << std::endl;

  int end_date = election->end_date().seconds();
  std::string choices_val = "";
  for (int i = 0; i < election->choices_size(); i++)
  {
    choices_val += election->choices(i) + " ";
  }
  // INSERT INTO Election(name , groups , end_date) VALUES ( 'name' , 'groups' , end_date );
  pqxx::connection c(DBCONNECTINFO);
  pqxx::result r;
  pqxx::work tx(c);
  std::string insert_val("'" + name + "' , '" + group_str + "' , '" + choices_val + "' , " + std::to_string(end_date));
  std::string sql("INSERT INTO Election (name , groups , choices , end_date) VALUES (" + insert_val + ");");

  // std::cout << "sql:" << sql << std::endl;
  try
  {
    r = tx.exec(sql);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    std::cerr << e.what() << '\n';
    status->set_code(3);
    return Status::OK;
  }

  // CREATE TABLE electionName ...
  pqxx::connection c2(DBCONNECTINFO);
  pqxx::result r2;
  pqxx::work tx2(c2);
  std::string create_val = "";
  for (int i = 0; i < election->choices_size(); i++)
  {
    create_val += election->choices(i);
    create_val += " INTEGER ,";
  }
  // std::cout << "create_val:" << create_val << std::endl;
  std::string sql2("CREATE TABLE " + name + " (" + create_val + " voter VARCHAR(20));");
  // std::cout << "sql:" << sql2 << std::endl;
  try
  {
    r2 = tx2.exec(sql2);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    c2.disconnect();
    std::cerr << e.what() << '\n';
    status->set_code(3);
    return Status::OK;
  }
  tx.commit();
  tx2.commit();
  c.disconnect();
  c2.disconnect();
  status->set_code(0);
  return Status::OK;
}

Status eVotingServer::CastVote(ServerContext *context, const Vote *vote, voting::Status *status)
{
  // check token
  std::string token = vote->token().value();
  if (!isAuth(token))
  {
    status->set_code(1);
    return Status::OK;
  }

  // SELECT electionName FROM electionTable
  std::string election_name = vote->election_name();
  pqxx::connection c(DBCONNECTINFO);
  pqxx::result r;
  pqxx::work tx(c);
  std::string sql("SELECT * FROM Election WHERE name = '" + election_name + "';");
  // std::cout << "sql:" << sql << std::endl;
  try
  {
    r = tx.exec(sql);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    std::cerr << e.what() << '\n';
    return Status::CANCELLED;
  }

  // Invalid election name
  if (r.size() == 0)
  {
    std::cout << "Invalid election name" << std::endl;
    c.disconnect();
    status->set_code(2);
    return Status::OK;
  }

  std::string electionGroups;
  std::time_t endtime;

  pqxx::field field = r[0][1];
  electionGroups = field.c_str();
  // std::cout << "groups:" << electionGroups << std::endl;

  field = r[0][3];
  std::string temp_str = field.c_str();
  endtime = std::stoll(temp_str);

  // election is not ongoing
  if (time(NULL) - endtime > 0)
  {
    // std::cout << "time diff:" << time(NULL) - endtime << std::endl;
    c.disconnect();
    status->set_code(2);
    return Status::OK;
  }
  tx.commit();
  c.disconnect();

  // get voter name
  std::string voter_name = "";
  for (int i = 0; i < userlist.size(); i++)
  {
    if (token == userlist[i].token)
    {
      voter_name = userlist[i].name;
      break;
    }
  }

  // get voter group
  pqxx::connection c2(DBCONNECTINFO);
  pqxx::result r2;
  pqxx::work tx2(c2);
  sql = "SELECT groups FROM Voter WHERE name = '" + voter_name + "';";
  try
  {
    r2 = tx2.exec(sql);
  }
  catch (const std::exception &e)
  {
    c2.disconnect();
    std::cerr << e.what() << '\n';
    return Status::CANCELLED;
  }

  field = r2[0][0];
  temp_str = field.c_str();
  std::vector<std::string> voter_group;
  std::stringstream s2(temp_str);
  while (true)
  {
    std::string tmp;
    s2 >> tmp;
    if (s2.fail())
      break;
    // std::cout << "group:" << tmp;
    voter_group.push_back(tmp);
  }
  // std::cout << std::endl;
  tx2.commit();
  c2.disconnect();

  // check group
  bool groupFlag = false;
  std::stringstream s1(electionGroups);
  while (true)
  {
    std::string tmp;
    s1 >> tmp;
    if (s1.fail())
      break;
    for (size_t i = 0; i < voter_group.size(); i++)
    {
      if (tmp == voter_group[i])
        groupFlag = true;
    }
  }
  if (!groupFlag)
  {
    // Status.code=3 : The voter’s group is not allowed in the election
    status->set_code(3);
    return Status::OK;
  }

  // check previous vote
  pqxx::connection c3(DBCONNECTINFO);
  sql = "SELECT Voter FROM " + election_name + ";";
  pqxx::result r3;
  pqxx::work tx3(c3);
  try
  {
    r3 = tx3.exec(sql);
  }
  catch (const std::exception &e)
  {
    c3.disconnect();
    std::cerr << e.what() << '\n';
    return Status::OK;
  }

  const int num_rows = r.size();
  for (int rownum = 0; rownum < num_rows; ++rownum)
  {
    const pqxx::row row = r[rownum];
    const int num_cols = row.size();
    for (int colnum = 0; colnum < num_cols; ++colnum)
    {
      const pqxx::field field = row[colnum];
      if (field.c_str() == voter_name)
      {
        // previous vote
        status->set_code(4);
        return Status::OK;
      }
    }
  }
  tx3.commit();
  c3.disconnect();
  // SELECT voter_name FROM electionName WHERE voter_name == name

  // INSERT INTO {election_name}( {choice_name} , Voter)  VALUES ( 1 , {voter_name} );
  pqxx::connection c4(DBCONNECTINFO);
  pqxx::result r4;
  pqxx::work tx4(c4);
  std::string choice_name = vote->choice_name();
  sql = "INSERT INTO " + election_name + " ( " + choice_name + " , Voter )  VALUES ( 1 , '" + voter_name + "' );";
  // std::cout << sql << std::endl;
  try
  {
    r4 = tx4.exec(sql);
  }
  catch (const std::exception &e)
  {
    c4.disconnect();
    std::cerr << e.what() << '\n';
    return Status::CANCELLED;
  }
  tx4.commit();
  c4.disconnect();
  status->set_code(0);
  return Status::OK;
}

Status eVotingServer::GetResult(ServerContext *context, const ElectionName *electionName, ElectionResult *electionResult)
{
  // SELECT electionName FROM electionTable checkElectionExist
  // exist , ongoing , or OK
  std::string election_name = electionName->name();
  pqxx::connection c(DBCONNECTINFO);
  pqxx::result r;
  pqxx::work tx(c);
  std::string sql("SELECT choices , end_date FROM Election WHERE name = '" + election_name + "';");
  // std::cout << "sql:" << sql << std::endl;
  try
  {
    r = tx.exec(sql);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    std::cerr << e.what() << '\n';
    electionResult->set_status(1);
    return Status::OK;
  }
  tx.commit();
  c.disconnect();

  // ElectionResult.status = 1: Non-existent election
  if (r.size() == 0)
  {
    std::cout << "electionName not exist\n";
    electionResult->set_status(1);
    return Status::OK;
  }

  // check the election is still ongoing.
  std::time_t endtime;
  pqxx::field field = r[0][1];
  std::string temp_str = field.c_str();
  endtime = std::stoll(temp_str);
  // std::cout << "time different:" << time(NULL) - endtime << std::endl;
  if (time(NULL) - endtime < 0)
  {
    std::cout << "election is still ongoing\n";
    electionResult->set_status(2);
    return Status::OK;
  }

  // get choice_name
  std::vector<std::string> choicesList;
  field = r[0][0];
  temp_str = field.c_str();
  std::stringstream s2(temp_str);
  while (true)
  {
    std::string tmp;
    s2 >> tmp;
    if (s2.fail())
      break;
    // std::cout << "choice_name:" << tmp;
    choicesList.push_back(tmp);
  }

  // get vote count FROM electionName
  pqxx::connection c2(DBCONNECTINFO);
  pqxx::result r2;
  pqxx::work tx2(c2);
  sql = "SELECT * FROM " + election_name + ";";
  // std::cout << "sql:" << sql << std::endl;
  try
  {
    r2 = tx2.exec(sql);
  }
  catch (const std::exception &e)
  {
    c2.disconnect();
    std::cerr << e.what() << '\n';
    return Status::CANCELLED;
  }
  tx2.commit();
  c2.disconnect();

  // list of choices counts
  std::vector<int> countsList;
  int num_rows = r2.size();
  const pqxx::row tmp_row = r2[num_rows];
  int countslen = tmp_row.size() - 1;
  for (int i = 0; i < countslen; i++)
  {
    countsList.push_back(0);
  }

  for (int rownum = 0; rownum < num_rows; ++rownum)
  {
    const pqxx::row row = r2[rownum];
    const int num_cols = row.size();
    for (int colnum = 0; colnum < num_cols - 1; ++colnum)
    {
      const pqxx::field field = row[colnum];
      temp_str = field.c_str();
      // std::cout << temp_str << '\t';
      if (temp_str == "1")
        countsList[colnum]++;
    }
  }
  VoteCount *vc;
  for (int i = 0; i < countslen; i++)
  {
    vc = new VoteCount();
    vc->set_choice_name(choicesList[i]);
    vc->set_count(countsList[i]);
    electionResult->add_counts()->CopyFrom(*vc);
  }
  electionResult->set_status(0);
  return Status::OK;
}

void RunServer(std::string server_address)
{
  eVotingServer service;
  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;
  server->Wait();
}

void unregister_voter()
{
  std::string name;
  std::cout << "input unregister voter name:";
  std::cin >> name;

  VoterName vname;
  vname.set_name(name);
  voting::Status vstatus = UnregisterVoter(vname);
  if (vstatus.code() == 0)
  {
    std::cout << "Successful unregistration\n";
  }
  else
  {
    if (vstatus.code() == 1)
      std::cout << "No voter with the name exists on the server\n";
    else
      std::cout << "Undefined error\n";
  }
}

void register_new_voter()
{
  std::string name, group;
  std::cout << "input new voter name:";
  std::cin >> name;
  std::cout << "input new voter group:";
  std::cin >> group;

  Voter newVoter;
  newVoter.set_name(name);
  newVoter.set_group(group);

  // create a key pair
  unsigned char pk[crypto_sign_PUBLICKEYBYTES];
  unsigned char sk[crypto_sign_SECRETKEYBYTES];
  crypto_sign_keypair(pk, sk);

  newVoter.set_public_key((char *)pk);
  voting::Status vstatus = RegisterVoter(newVoter);
  if (vstatus.code() == 0)
  {
    std::cout << "Successful registration\n";
    std::fstream file;
    std::string filename;
    filename = "key/" + name + "pk";
    std::cout << "public key save in " << filename << std::endl;
    // std::cout << pk << std::endl;
    file.open(filename, std::fstream::out | std::fstream::binary);
    file.write((char *)pk, crypto_sign_PUBLICKEYBYTES);
    file.close();

    filename = "key/" + name + "sk";
    std::cout << "secret key save in " << filename << std::endl;
    // std::cout << sk << std::endl;
    file.open(filename, std::fstream::out | std::fstream::binary);
    file.write((char *)sk, crypto_sign_SECRETKEYBYTES);
    file.close();
  }
  else if (vstatus.code() == 1)
    std::cout << "Voter with the same name already exists\n";
  else
    std::cout << "Undefined error\n";
  return;
}

int initDB()
{
  pqxx::connection c(DBCONNECTINFO);
  std::string sql;
  pqxx::result r;
  pqxx::work tx(c);

  sql = "CREATE TABLE Voter (name VARCHAR(20) NOT NULL, groups VARCHAR(20), public_key varbit(64));\
  CREATE TABLE Election (name VARCHAR(20) NOT NULL, groups VARCHAR(50), choices VARCHAR(50), end_date INTEGER);";
  try
  {
    r = tx.exec(sql);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    std::cerr << e.what() << '\n';
    return 1;
  }
  tx.commit();
  c.disconnect();
  return 0;
}

int printVoter()
{
  pqxx::connection c(DBCONNECTINFO);
  std::string sql("SELECT name, groups FROM Voter;");
  pqxx::result r;
  pqxx::work tx(c);
  try
  {
    r = tx.exec(sql);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    std::cerr << e.what() << '\n';
    return 1;
  }

  const int num_rows = r.size();
  if (num_rows == 0)
    std::cout << "query empty" << std::endl;
  for (int rownum = 0; rownum < num_rows; ++rownum)
  {
    const pqxx::row row = r[rownum];
    const int num_cols = row.size();
    for (int colnum = 0; colnum < num_cols; ++colnum)
    {
      const pqxx::field field = row[colnum];
      std::cout << field.c_str() << '\t';
    }
    std::cout << std::endl;
  }
  tx.commit();
  c.disconnect();
  return 0;
}

int printElection()
{
  pqxx::connection c(DBCONNECTINFO);
  std::string sql("SELECT name, groups, end_date FROM Election;");
  pqxx::result r;
  pqxx::work tx(c);
  try
  {
    r = tx.exec(sql);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    std::cerr << e.what() << '\n';
    return 1;
  }

  const int num_rows = r.size();
  if (num_rows == 0)
    std::cout << "query empty" << std::endl;
  for (int rownum = 0; rownum < num_rows; ++rownum)
  {
    const pqxx::row row = r[rownum];
    const int num_cols = row.size();
    for (int colnum = 0; colnum < num_cols; ++colnum)
    {
      const pqxx::field field = row[colnum];
      std::cout << field.c_str() << '\t';
    }
    std::cout << std::endl;
  }
  tx.commit();
  c.disconnect();
  return 0;
}

int printVote(std::string election)
{
  pqxx::connection c(DBCONNECTINFO);
  std::string sql("SELECT * FROM " + election + ";");
  pqxx::result r;
  pqxx::work tx(c);
  try
  {
    r = tx.exec(sql);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    std::cerr << e.what() << '\n';
    return 1;
  }

  const int num_rows = r.size();
  if (num_rows == 0)
    std::cout << "query empty" << std::endl;
  for (int rownum = 0; rownum < num_rows; ++rownum)
  {
    const pqxx::row row = r[rownum];
    const int num_cols = row.size();
    for (int colnum = 0; colnum < num_cols; ++colnum)
    {
      const pqxx::field field = row[colnum];
      std::cout << field.c_str() << '\t';
    }
    std::cout << std::endl;
  }
  tx.commit();
  c.disconnect();
  return 0;
}

void controller(pid_t pid)
{
  std::string str;
  std::string instruction("\t 'init' to initial the database\n\t 'r' to register voter\n\t 'u' to unregister voter\n\t 'help' or 'h' to show the instruction\n\t 'exit' to close server\n");
  std::cout << "There is the server controller, please input the instruction to control the voting server\n"
            << instruction << "% ";
  while (std::cin >> str)
  {
    if (str == "exit")
      break;
    else if (str == "init")
      if (initDB() == 0)
        std::cout << "initital database success\n";
      else
        std::cout << "initital database fall\n";
    else if (str == "r")
      register_new_voter();
    else if (str == "u")
      unregister_voter();
    // else if (str == "voter")
    //   printVoter();
    // else if (str == "election")
    //   printElection();
    // else if (str == "pv")
    // {
    //   std::string t;
    //   std::cin >> t;
    //   printVote(t);
    // }
    else if (str == "help" || str == "h")
      std::cout << instruction;
    else
      std::cout << "invalid command\n";
    std::cout << "% ";
  }

  // close child process
  int status;
  if (kill(pid, SIGTERM) == 0)
    std::cout << "server close\n";
  else
    kill(pid, SIGKILL);
  waitpid(pid, &status, 0);
  return;
}

int main(int argc, char **argv)
{
  // server port
  std::string server_address("0.0.0.0:");
  if (argc == 1)
    server_address += "50051";
  else
    server_address += argv[1];

  // key
  if (sodium_init() < 0)
  {
    std::cerr << "initializes libsodium error\n";
    return 1;
  }

  // try connect to Database
  try
  {
    dbConnect = new pqxx::connection(DBCONNECTINFO);
    if (dbConnect->is_open())
    {
      std::cout << "Opened database successfully: " << dbConnect->dbname() << std::endl;
    }
  }
  catch (const std::exception &e)
  {
    std::cerr << e.what() << '\n';
    std::cerr << "Can't open database" << std::endl;
    return 1;
  }
  dbConnect->disconnect();
  delete dbConnect;

  pid_t pid = fork();
  // child process run rpc server
  if (pid == 0)
    RunServer(server_address);
  else // parent process run server controler
    controller(pid);
  return 0;
}
