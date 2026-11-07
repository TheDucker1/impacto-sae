#include "titlesession.h"
#include "commands.h"

namespace Impacto::Saekano {
namespace {
void Validate(const TitleSessionSeeds& seeds){
  for(auto index:seeds.InitialKeywords)if(index<0 || index>=1024)throw FormatError("initial title keyword outside bank0");
  if(seeds.Stats.RowCount>4)throw FormatError("title profile seed row outside owner");
  for(size_t i=0;i<seeds.Stats.RowCount;++i)if(seeds.Stats.RowLengths[i]>3)throw FormatError("title profile seed column outside owner");
}
}
TitleSessionSeeds TitleSessionSeeds::Decode(std::span<const uint8_t> keyword,std::span<const uint8_t> profile){
  TitleSessionSeeds seeds;PayloadReader r(keyword);for(auto& flag:seeds.KeywordFlags)flag=r.U8();
  const auto count=r.U32();r.CheckCount(count,4);if(count>1000000)throw FormatError("title keyword seed count exceeds budget");
  seeds.InitialKeywords.reserve(count);for(uint32_t i=0;i<count;++i)seeds.InitialKeywords.push_back(r.I32());
  seeds.KeywordPrefixBytes=keyword.size()-r.Remaining();seeds.Stats=ProfileInitialStats::Decode(profile);Validate(seeds);return seeds;
}
void ResetTitleSession(SemanticState& owner,const TitleSessionSeeds& seeds){
  Validate(seeds); // Validate before publishing any working-state write.
  owner.TitleSession=TitleSessionFields{};owner.LastBackgroundId=-1;owner.Bank0.fill(0);
  for(auto index:seeds.InitialKeywords)owner.Bank0[size_t(index)]=1;
  for(size_t row=0;row<seeds.Stats.RowCount;++row)for(size_t column=0;column<seeds.Stats.RowLengths[row];++column)
    owner.ProfileOwner.Stats[row*3+column]=seeds.Stats.Values[row*3+column];
}
}
